// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
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
#include "core/loader/loader.h"
#include "video_core/overlay.h"

namespace SwitchFrontend {

namespace {

// The kind of row. Cheat rows are appended once per loaded cheat on the current page, so a row
// also carries the cheat's index into the engine's list.
enum class Item {
    ScreenLayout,
    GyroSensitivityX,
    GyroSensitivityY,
    PointerSource,
    PointerMode,
    FpsCounter,
    CustomTextures,
    TextureFilter,
    RightEyeRender,
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

// The overlay is split into tabs with L/R used to cycle between them.
enum class Tab {
    Settings,
    Cheats,
};

// Percentage step for the gyro sensitivity rows.
constexpr int kGyroStep = 10;

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

// Cheats past this many spill onto further pages so the panel never overflows the screen.
constexpr int kCheatsPerPage = 8;

std::atomic<bool> s_open{false};
Tab s_tab = Tab::Settings;
int s_selected = 0;
int s_cheat_page = 0;
std::vector<Row> s_rows;
bool s_cheats_dirty = false;

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

// Rebuilds the visible rows for the active tab and page and keeps the cursor in range.
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

// Rebuilds the visible rows for the active tab and page and keeps the cursor in range.
void RebuildRows() {
    s_rows.clear();
    if (s_tab == Tab::Settings) {
        s_rows.push_back({Item::ScreenLayout});
        s_rows.push_back({Item::GyroSensitivityX});
        s_rows.push_back({Item::GyroSensitivityY});
        s_rows.push_back({Item::PointerSource});
        s_rows.push_back({Item::PointerMode});
        s_rows.push_back({Item::FpsCounter});
        s_rows.push_back({Item::CustomTextures});
        s_rows.push_back({Item::TextureFilter});
        s_rows.push_back({Item::RightEyeRender});
        s_rows.push_back({Item::Resume});
        s_rows.push_back({Item::ExitGame});
    } else {
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
    }
    s_selected = std::clamp(s_selected, 0, static_cast<int>(s_rows.size()) - 1);
}

bool IsAction(const Row& row) {
    return row.item == Item::Resume || row.item == Item::ExitGame ||
           row.item == Item::CheatsEmpty || row.item == Item::AddCheat;
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
        return GetPointerSource() == PointerSource::Gyro ? "Gyro" : "Left Stick";
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
    case Item::Cheat:
        return CheatEnabled(row.cheat_index) ? "On" : "Off";
    default:
        return "";
    }
}

// Left/right on a value row. `dir` is -1 or +1.
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
        SetPointerSource(dir > 0 ? PointerSource::Gyro : PointerSource::Stick);
        break;
    case Item::PointerMode:
        SetPointerMode(dir > 0);
        break;
    case Item::FpsCounter:
        Settings::values.show_fps = dir > 0;
        break;
    case Item::CustomTextures:
        Settings::values.custom_textures = dir > 0;
        break;
    case Item::TextureFilter: {
        const int current = static_cast<int>(Settings::values.texture_filter.GetValue());
        Settings::values.texture_filter =
            static_cast<Settings::TextureFilter>(std::clamp(current + dir, 0, 5));
        break;
    }
    case Item::RightEyeRender:
        Settings::values.disable_right_eye_render = dir > 0;
        break;
    case Item::Cheat:
        ToggleCheat(row.cheat_index);
        break;
    default:
        break;
    }
}

// Pressing 'a' on a row advances the list by one.
void Activate(const Row& row) {
    switch (row.item) {
    case Item::PointerSource:
        SetPointerSource(GetPointerSource() == PointerSource::Gyro ? PointerSource::Stick
                                                                   : PointerSource::Gyro);
        break;
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
        Adjust(row, 1);
        break;
    }
}

void Repaint() {
    VideoCore::OverlayMenuState state;
    state.visible = s_open.load(std::memory_order_relaxed);
    state.selected = s_selected;
    if (s_tab == Tab::Settings) {
        state.title = "Quick Menu - Settings";
        state.hint = "A Change   L/R Tab   +/- Close";
    } else {
        const int pages = CheatPageCount();
        state.title = "Quick Menu - Cheats";
        std::string hint = "A Toggle";
        const bool on_cheat_row = s_selected >= 0 &&
                                  s_selected < static_cast<int>(s_rows.size()) &&
                                  s_rows[s_selected].item == Item::Cheat;
        if (on_cheat_row) {
            hint += "   X Edit   Y Delete";
        }
        hint += "   L/R Tab";
        if (pages > 1) {
            state.title += " (Page " + std::to_string(s_cheat_page + 1) + "/" +
                           std::to_string(pages) + ")";
            hint += "   ZL/ZR Page";
        }
        hint += "   +/- Close";
        state.hint = hint;
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
    s_tab = Tab::Settings;
    s_selected = 0;
    s_cheat_page = 0;
    RebuildRows();
    s_open.store(true, std::memory_order_relaxed);
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

    if (nav.cancel) {
        CloseQuickMenu();
        return QuickMenuAction::Close;
    }

    bool changed = false;

    // L/R jump between the Settings and Cheats tabs.
    if (nav.tab_prev || nav.tab_next) {
        s_tab = s_tab == Tab::Settings ? Tab::Cheats : Tab::Settings;
        s_selected = 0;
        RebuildRows();
        changed = true;
    }

    // ZL/ZR page through the cheat list.
    if (s_tab == Tab::Cheats && (nav.page_prev || nav.page_next)) {
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
    if (nav.left && !IsAction(row)) {
        Adjust(row, -1);
        changed = true;
    }
    if (nav.right && !IsAction(row)) {
        Adjust(row, 1);
        changed = true;
    }
    if (nav.confirm) {
        if (row.item == Item::Resume) {
            CloseQuickMenu();
            return QuickMenuAction::Close;
        }
        if (row.item == Item::ExitGame) {
            CloseQuickMenu();
            return QuickMenuAction::ExitGame;
        }
        Activate(row);
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
