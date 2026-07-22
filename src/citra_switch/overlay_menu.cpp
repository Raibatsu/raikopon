// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/keyboard_prompt.h"
#include "citra_switch/overlay_menu.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
#include "core/cheats/gateway_cheat.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/loader/loader.h"
#include "video_core/overlay.h"

namespace SwitchFrontend {

namespace {

// The kind of row. Cheat rows are appended once per loaded cheat on the current page, so a row
// also carries the cheat's index into the engine's list.
enum class Item {
    ScreenLayout,
    FpsCounter,
    CustomTextures,
    TextureFilter,
    RightEyeRender,
    PointerSource,
    PointerMode,
    GyroSensitivityX,
    GyroSensitivityY,
    CpuClock,
    MovieThrottleClock,
    AddCheat,
    Cheat,
    CheatsEmpty,
    Resume,
    ExitGame,
};

struct Row {
    Item item;
    int cheat_index = -1;
};

// The overlay is split into pages with L/R used to cycle between them.
enum class Page {
    Display,
    Input,
    System,
    Cheats,
};

constexpr std::array<Page, 4> kPages = {Page::Display, Page::Input, Page::System, Page::Cheats};

const char* PageName(Page page) {
    switch (page) {
    case Page::Display:
        return "Display";
    case Page::Input:
        return "Input";
    case Page::System:
        return "System";
    case Page::Cheats:
        return "Cheats";
    }
    return "";
}

// Percentage step for the gyro sensitivity rows.
constexpr int kGyroStep = 10;

// Percentage step for the movie CPU throttle row. 1 (rather than a coarser step like the
// System page's "CPU Clock" row) so it behaves like a freely adjustable slider, especially
// combined with the quick menu's hold-to-repeat on left/right (see citra_switch.cpp).
constexpr int kMovieThrottleStep = 1;

// Percentage step and range for the emulated CPU clock.
constexpr int kClockStep = 25;
constexpr int kClockMin = 25;
constexpr int kClockMax = 400;

const char* TextureFilterName(int filter) {
    switch (filter) {
    case 0:
        return "None";
    case 1:
        return "Anime4K";
    case 2:
        return "Bicubic";
    case 3:
        return "ScaleForce";
    case 4:
        return "xBRZ";
    case 5:
        return "MMPX";
    default:
        return "None";
    }
}

// Cheats past this many spill onto further sub-pages.
constexpr int kCheatsPerPage = 8;

std::atomic<bool> s_open{false};
int s_page = 0;
int s_selected = 0;
int s_cheat_page = 0;
std::vector<Row> s_rows;
bool s_cheats_dirty = false;
// True while the selected row is armed: joystick left/right adjusts it instead of moving the
// cursor. Mirrors the Settings screen's arm/select model (see menu.cpp) so a stick that isn't
// perfectly centered can't silently nudge a value while the player is just scrolling.
bool s_armed = false;

Page CurrentPage() {
    return kPages[static_cast<std::size_t>(s_page)];
}

Cheats::CheatEngine* GetCheatEngine() {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return nullptr;
    }
    return &system.CheatEngine();
}

int CheatCount() {
    auto* engine = GetCheatEngine();
    return engine ? static_cast<int>(engine->GetCheats().size()) : 0;
}

int CheatPageCount() {
    const int count = CheatCount();
    return count <= 0 ? 1 : (count + kCheatsPerPage - 1) / kCheatsPerPage;
}

std::string CheatName(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return "";
    }
    const auto cheats = engine->GetCheats();
    return index >= 0 && index < static_cast<int>(cheats.size()) ? cheats[index]->GetName() : "";
}

bool CheatEnabled(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return false;
    }
    const auto cheats = engine->GetCheats();
    return index >= 0 && index < static_cast<int>(cheats.size()) && cheats[index]->IsEnabled();
}

void ToggleCheat(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return;
    }
    const auto cheats = engine->GetCheats();
    if (index < 0 || index >= static_cast<int>(cheats.size())) {
        return;
    }
    cheats[index]->SetEnabled(!cheats[index]->IsEnabled());
    s_cheats_dirty = true;
}

// Writes the enabled state the player just picked back to the cheat file so it sticks.
void PersistCheats() {
    if (!s_cheats_dirty) {
        return;
    }
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        u64 title_id = 0;
        system.GetAppLoader().ReadProgramId(title_id);
        system.CheatEngine().SaveCheatFile(title_id);
    }
    s_cheats_dirty = false;
}

// Rebuilds the visible rows for the active page and keeps the cursor in range.
// Forward-declared so EditCheatFlow/DeleteCheatFlow (which need to land the cursor on the
// affected cheat) can call it.
void RebuildRows();

// Puts the cursor on cheat `index`'s row, on whatever page it falls on. `index` may be one past
// the last cheat (used right after a delete) or exactly the row count (right after an add) —
// both land on the AddCheat row instead, which is always a safe fallback.
void SelectCheat(int index) {
    s_cheat_page = index / kCheatsPerPage;
    RebuildRows();
    const int first = s_cheat_page * kCheatsPerPage;
    const int wanted = 1 + (index - first); // +1 for the pinned Add Cheat row.
    s_selected = std::clamp(wanted, 0, static_cast<int>(s_rows.size()) - 1);
}

// Runs the add/modify cheat flow: prompts for a name, then the Gateway-format code one line at a
// time (rather than as one multi-line field — this doesn't depend on how the system keyboard's
// Return key handles embedded newlines, which is otherwise a real source of "the cheat silently
// does nothing" if a multi-line paste doesn't come back split the way the parser expects).
// `edit_index` >= 0 modifies that existing cheat (fields pre-filled with its current name/lines);
// -1 creates a new one, which starts disabled, matching the desktop Cheats dialog's own default —
// flip it on afterwards with the same A/left-right toggle as any other cheat row. No-ops (leaves
// the existing cheat, if any, untouched) if the user cancels the name prompt or enters zero code
// lines.
void EditCheatFlow(int edit_index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return;
    }

    std::string initial_name;
    std::vector<std::string> initial_lines;
    if (edit_index >= 0) {
        const auto cheats = engine->GetCheats();
        if (edit_index >= static_cast<int>(cheats.size())) {
            return;
        }
        initial_name = cheats[edit_index]->GetName();
        initial_lines = Common::SplitString(cheats[edit_index]->GetCode(), '\n');
        // GetCode() puts a trailing '\n' after the last line too, which SplitString turns into
        // one trailing empty entry.
        while (!initial_lines.empty() && initial_lines.back().empty()) {
            initial_lines.pop_back();
        }
    }

    const std::string name = PromptKeyboard("Cheat name", "e.g. Infinite HP", initial_name, 64);
    if (name.empty()) {
        return;
    }

    std::vector<std::string> lines;
    for (std::size_t i = 0;; ++i) {
        const std::string existing = i < initial_lines.size() ? initial_lines[i] : std::string{};
        const std::string header = "Cheat code - line " + std::to_string(i + 1);
        const std::string guide = existing.empty() ? "XXXXXXXX YYYYYYYY - blank line to finish"
                                                    : "XXXXXXXX YYYYYYYY - blank clears this line";
        // Stripped defensively: GatewayCheat's line parser requires exactly 17 characters
        // ("XXXXXXXX YYYYYYYY") with no slack, and a touch keyboard is an easy way to pick up
        // a stray leading/trailing space that would otherwise silently invalidate the line.
        const std::string line = Common::StripSpaces(PromptKeyboard(header, guide, existing, 32));
        if (line.empty()) {
            break;
        }
        lines.push_back(line);
    }
    if (lines.empty()) {
        return;
    }

    std::string code;
    for (const std::string& line : lines) {
        code += line + '\n';
    }

    auto cheat = std::make_shared<Cheats::GatewayCheat>(name, code, std::string{});
    int result_index = edit_index;
    if (edit_index >= 0) {
        engine->UpdateCheat(static_cast<std::size_t>(edit_index), std::move(cheat));
    } else {
        result_index = CheatCount();
        engine->AddCheat(std::move(cheat));
    }
    s_cheats_dirty = true;
    SelectCheat(result_index);
}

// Deletes cheat `index` after confirming the row is still valid. No confirmation prompt, matching
// this codebase's other immediate-effect resets (e.g. RemapControls' Y = Reset in menu.cpp) — the
// overlay has no free-form modal to show one in anyway.
void DeleteCheatFlow(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return;
    }
    if (index < 0 || index >= CheatCount()) {
        return;
    }
    engine->RemoveCheat(static_cast<std::size_t>(index));
    s_cheats_dirty = true;
    SelectCheat(index);
}

void RebuildRows() {
    s_rows.clear();
    switch (CurrentPage()) {
    case Page::Display:
        s_rows.push_back({Item::ScreenLayout});
        s_rows.push_back({Item::FpsCounter});
        s_rows.push_back({Item::CustomTextures});
        s_rows.push_back({Item::TextureFilter});
        s_rows.push_back({Item::RightEyeRender});
        break;
    case Page::Input:
        s_rows.push_back({Item::PointerSource});
        s_rows.push_back({Item::PointerMode});
        s_rows.push_back({Item::GyroSensitivityX});
        s_rows.push_back({Item::GyroSensitivityY});
        break;
    case Page::System:
        s_rows.push_back({Item::CpuClock});
        s_rows.push_back({Item::MovieThrottleClock});
        s_rows.push_back({Item::Resume});
        s_rows.push_back({Item::ExitGame});
        break;
    case Page::Cheats: {
        s_rows.push_back({Item::AddCheat});
        const int count = CheatCount();
        s_cheat_page = std::clamp(s_cheat_page, 0, CheatPageCount() - 1);
        const int first = s_cheat_page * kCheatsPerPage;
        const int last = std::min(count, first + kCheatsPerPage);
        for (int i = first; i < last; ++i) {
            s_rows.push_back({Item::Cheat, i});
        }
        if (count == 0) {
            s_rows.push_back({Item::CheatsEmpty});
        }
        break;
    }
    }
    s_selected = std::clamp(s_selected, 0, static_cast<int>(s_rows.size()) - 1);
}

// The running timers keep their own copy of the clock scale, so a change has to be pushed into
// core timing to take effect without a reboot.
void SetCpuClock(int percent) {
    percent = std::clamp(percent, kClockMin, kClockMax);
    Settings::values.cpu_clock_percentage = percent;
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        system.CoreTiming().UpdateClockSpeed(static_cast<u32>(percent));
    }
}

bool IsAction(const Row& row) {
    return row.item == Item::Resume || row.item == Item::ExitGame ||
           row.item == Item::CheatsEmpty || row.item == Item::AddCheat;
}

// True for rows with only two states, toggled directly by an A press rather than armed for
// joystick adjustment (see s_armed).
bool IsBooleanItem(Item item) {
    switch (item) {
    case Item::FpsCounter:
    case Item::CustomTextures:
    case Item::RightEyeRender:
    case Item::PointerMode:
    case Item::Cheat:
        return true;
    default:
        return false;
    }
}

std::string Label(const Row& row) {
    switch (row.item) {
    case Item::ScreenLayout:
        return "Screen Layout";
    case Item::GyroSensitivityX:
        return "Gyro Sens. X";
    case Item::GyroSensitivityY:
        return "Gyro Sens. Y";
    case Item::PointerSource:
        return "Touch Pointer";
    case Item::PointerMode:
        return "Pointer Mode";
    case Item::FpsCounter:
        return "FPS Counter";
    case Item::CustomTextures:
        return "Custom Textures";
    case Item::TextureFilter:
        return "Texture Filter";
    case Item::RightEyeRender:
        return "Disable Right Eye";
    case Item::CpuClock:
        return "CPU Clock";
    case Item::MovieThrottleClock:
        return "Movie CPU Throttle";
    case Item::AddCheat:
        return "+ Add Cheat";
    case Item::Cheat:
        return CheatName(row.cheat_index);
    case Item::CheatsEmpty:
        return "No cheats loaded";
    case Item::Resume:
        return "Resume Game";
    case Item::ExitGame:
        return "Exit to Library";
    }
    return "";
}

std::string Value(const Row& row) {
    switch (row.item) {
    case Item::ScreenLayout:
        return CurrentScreenLayoutName();
    case Item::GyroSensitivityX:
        return std::to_string(GetGyroSensitivityX()) + "%";
    case Item::GyroSensitivityY:
        return std::to_string(GetGyroSensitivityY()) + "%";
    case Item::PointerSource:
        return PointerSourceName(GetPointerSource());
    case Item::PointerMode:
        return IsPointerModeActive() ? "On" : "Off";
    case Item::FpsCounter:
        return Settings::values.show_fps.GetValue() ? "On" : "Off";
    case Item::CustomTextures:
        return Settings::values.custom_textures.GetValue() ? "On" : "Off";
    case Item::TextureFilter:
        return TextureFilterName(static_cast<int>(Settings::values.texture_filter.GetValue()));
    case Item::RightEyeRender:
        return Settings::values.disable_right_eye_render.GetValue() ? "On" : "Off";
    case Item::CpuClock:
        return std::to_string(Settings::values.cpu_clock_percentage.GetValue()) + "%";
    case Item::MovieThrottleClock:
        return std::to_string(GetMovieThrottleClockPercentage()) + "%";
    case Item::Cheat:
        return CheatEnabled(row.cheat_index) ? "On" : "Off";
    default:
        return "";
    }
}

// Left/right on an armed value row. `dir` is -1 or +1. Only reachable for rows IsBooleanItem
// returns false for — booleans flip directly on an A press instead (see Activate).
void Adjust(const Row& row, int dir) {
    switch (row.item) {
    case Item::ScreenLayout:
        StepScreenLayout(dir);
        break;
    case Item::GyroSensitivityX:
        SetGyroSensitivity(GetGyroSensitivityX() + dir * kGyroStep, GetGyroSensitivityY());
        break;
    case Item::GyroSensitivityY:
        SetGyroSensitivity(GetGyroSensitivityX(), GetGyroSensitivityY() + dir * kGyroStep);
        break;
    case Item::PointerSource:
        SetPointerSource(static_cast<PointerSource>(std::clamp(
            static_cast<int>(GetPointerSource()) + dir, 0, NumPointerSources - 1)));
        break;
    case Item::TextureFilter: {
        const int current = static_cast<int>(Settings::values.texture_filter.GetValue());
        Settings::values.texture_filter =
            static_cast<Settings::TextureFilter>(std::clamp(current + dir, 0, 5));
        break;
    }
    case Item::CpuClock:
        SetCpuClock(Settings::values.cpu_clock_percentage.GetValue() + dir * kClockStep);
        break;
    case Item::MovieThrottleClock:
        SetMovieThrottleClockPercentage(GetMovieThrottleClockPercentage() + dir * kMovieThrottleStep);
        break;
    default:
        break;
    }
}

// Pressing 'A' on an action or boolean row (see IsAction/IsBooleanItem). Value rows arm instead
// of calling this — see UpdateQuickMenu.
void Activate(const Row& row) {
    switch (row.item) {
    case Item::PointerMode:
        TogglePointerMode();
        break;
    case Item::FpsCounter:
        Settings::values.show_fps = !Settings::values.show_fps.GetValue();
        break;
    case Item::CustomTextures:
        Settings::values.custom_textures = !Settings::values.custom_textures.GetValue();
        break;
    case Item::RightEyeRender:
        Settings::values.disable_right_eye_render =
            !Settings::values.disable_right_eye_render.GetValue();
        break;
    case Item::AddCheat:
        EditCheatFlow(-1);
        break;
    case Item::Cheat:
        ToggleCheat(row.cheat_index);
        break;
    default:
        break;
    }
}

void Repaint() {
    VideoCore::OverlayMenuState state;
    state.visible = s_open.load(std::memory_order_relaxed);
    state.selected = s_selected;
    state.armed = s_armed;
    state.title = std::string("Quick Menu - ") + PageName(CurrentPage()) + " (" +
                  std::to_string(s_page + 1) + "/" + std::to_string(kPages.size()) + ")";
    if (s_armed) {
        state.hint = "<> Adjust   A / B Done";
    } else if (CurrentPage() == Page::Cheats) {
        const int cheat_pages = CheatPageCount();
        std::string hint = "A Toggle";
        const bool on_cheat_row = s_selected >= 0 &&
                                  s_selected < static_cast<int>(s_rows.size()) &&
                                  s_rows[s_selected].item == Item::Cheat;
        if (on_cheat_row) {
            hint += "   X Edit   Y Delete";
        }
        hint += "   L/R Page";
        if (cheat_pages > 1) {
            state.title += "  List " + std::to_string(s_cheat_page + 1) + "/" +
                           std::to_string(cheat_pages);
            hint += "   ZL/ZR List";
        }
        hint += "   +/- Close";
        state.hint = hint;
    } else {
        const bool row_valid = s_selected >= 0 && s_selected < static_cast<int>(s_rows.size());
        const bool a_toggles =
            row_valid && (IsAction(s_rows[s_selected]) || IsBooleanItem(s_rows[s_selected].item));
        state.hint =
            std::string("A ") + (a_toggles ? "Toggle" : "Select") + "   L/R Page   +/- Close";
    }
    state.items.reserve(s_rows.size());
    for (const Row& row : s_rows) {
        state.items.push_back({Label(row), Value(row), IsAction(row)});
    }
    VideoCore::SetOverlayMenuState(state);
}

} // namespace

bool IsQuickMenuOpen() {
    return s_open.load(std::memory_order_relaxed);
}

void OpenQuickMenu() {
    s_page = 0;
    s_selected = 0;
    s_cheat_page = 0;
    s_armed = false;
    RebuildRows();
    s_open.store(true, std::memory_order_relaxed);
    PauseEmulation();
    Repaint();
}

void CloseQuickMenu() {
    const bool was_open = s_open.exchange(false, std::memory_order_relaxed);
    VideoCore::OverlayMenuState state;
    state.visible = false;
    VideoCore::SetOverlayMenuState(state);
    // Persist the settings the player changed.
    if (was_open) {
        PersistCheats();
        SaveConfig();
        ResumeEmulation();
    }
}

void ToggleQuickMenu() {
    if (IsQuickMenuOpen()) {
        CloseQuickMenu();
    } else {
        OpenQuickMenu();
    }
}

QuickMenuAction UpdateQuickMenu(const QuickMenuNav& nav) {
    if (!IsQuickMenuOpen()) {
        return QuickMenuAction::None;
    }

    // While armed, the stick only adjusts the selected row — no page/cursor navigation, and
    // A or B un-arms instead of closing the menu. Mirrors menu.cpp's HandleSettings.
    if (s_armed) {
        bool changed = false;
        const Row& row = s_rows[s_selected];
        if (nav.left) {
            Adjust(row, -1);
            changed = true;
        }
        if (nav.right) {
            Adjust(row, 1);
            changed = true;
        }
        if (nav.confirm || nav.cancel) {
            s_armed = false;
            changed = true;
        }
        if (changed) {
            Repaint();
        }
        return QuickMenuAction::None;
    }

    if (nav.cancel) {
        CloseQuickMenu();
        return QuickMenuAction::Close;
    }

    bool changed = false;

    // L/R cycle through the menu's pages.
    if (nav.tab_prev != nav.tab_next) {
        const int count = static_cast<int>(kPages.size());
        s_page = (s_page + (nav.tab_next ? 1 : -1) + count) % count;
        s_selected = 0;
        RebuildRows();
        changed = true;
    }

    // ZL/ZR page through the cheat list.
    if (CurrentPage() == Page::Cheats && (nav.page_prev || nav.page_next)) {
        const int pages = CheatPageCount();
        const int dir = nav.page_next ? 1 : -1;
        s_cheat_page = (s_cheat_page + dir + pages) % pages;
        s_selected = 0;
        RebuildRows();
        changed = true;
    }

    const int count = static_cast<int>(s_rows.size());
    if (nav.up) {
        s_selected = (s_selected - 1 + count) % count;
        changed = true;
    }
    if (nav.down) {
        s_selected = (s_selected + 1) % count;
        changed = true;
    }

    // A copy, not a reference: Activate() below can run EditCheatFlow(), which rebuilds
    // s_rows (invalidating any reference into it) before this function reads `row` again for
    // the modify/delete check.
    const Row row = s_rows[s_selected];
    if (nav.confirm) {
        if (row.item == Item::Resume) {
            CloseQuickMenu();
            return QuickMenuAction::Close;
        }
        if (row.item == Item::ExitGame) {
            CloseQuickMenu();
            return QuickMenuAction::ExitGame;
        }
        if (IsAction(row) || IsBooleanItem(row.item)) {
            Activate(row);
        } else {
            // Value row: arm it instead of changing anything yet — joystick left/right takes
            // over next frame while armed.
            s_armed = true;
        }
        changed = true;
    }
    if (row.item == Item::Cheat) {
        if (nav.modify) {
            EditCheatFlow(row.cheat_index);
            changed = true;
        } else if (nav.remove) {
            DeleteCheatFlow(row.cheat_index);
            changed = true;
        }
    }

    if (changed) {
        Repaint();
    }
    return QuickMenuAction::None;
}

} // namespace SwitchFrontend
