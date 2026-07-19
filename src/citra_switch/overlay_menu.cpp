// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/overlay_menu.h"
#include "common/settings.h"
#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
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
        s_rows.push_back({Item::Resume});
        s_rows.push_back({Item::ExitGame});
    } else {
        const int count = CheatCount();
        s_cheat_page = std::clamp(s_cheat_page, 0, CheatPageCount() - 1);
        const int first = s_cheat_page * kCheatsPerPage;
        const int last = std::min(count, first + kCheatsPerPage);
        for (int i = first; i < last; ++i) {
            s_rows.push_back({Item::Cheat, i});
        }
        if (s_rows.empty()) {
            s_rows.push_back({Item::CheatsEmpty});
        }
    }
    s_selected = std::clamp(s_selected, 0, static_cast<int>(s_rows.size()) - 1);
}

bool IsAction(const Row& row) {
    return row.item == Item::Resume || row.item == Item::ExitGame ||
           row.item == Item::CheatsEmpty;
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
        if (pages > 1) {
            state.title += " (Page " + std::to_string(s_cheat_page + 1) + "/" +
                           std::to_string(pages) + ")";
            state.hint = "A Toggle   L/R Tab   ZL/ZR Page   +/- Close";
        } else {
            state.hint = "A Toggle   L/R Tab   +/- Close";
        }
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

    const Row& row = s_rows[s_selected];
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

    if (changed) {
        Repaint();
    }
    return QuickMenuAction::None;
}

} // namespace SwitchFrontend
