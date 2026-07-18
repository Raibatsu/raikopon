// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <string>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/overlay_menu.h"
#include "common/settings.h"
#include "video_core/overlay.h"

namespace SwitchFrontend {

namespace {

// The rows shown in the menu.
enum class Item {
    ScreenLayout,
    GyroSensitivityX,
    GyroSensitivityY,
    PointerSource,
    PointerMode,
    FpsCounter,
    Resume,
    ExitGame,
};

constexpr std::array<Item, 8> kItems = {
    Item::ScreenLayout, Item::GyroSensitivityX, Item::GyroSensitivityY, Item::PointerSource,
    Item::PointerMode,  Item::FpsCounter,       Item::Resume,           Item::ExitGame,
};

// Percentage step for the gyro sensitivity rows.
constexpr int kGyroStep = 10;

std::atomic<bool> s_open{false};
int s_selected = 0;

bool IsAction(Item item) {
    return item == Item::Resume || item == Item::ExitGame;
}

const char* Label(Item item) {
    switch (item) {
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
    case Item::Resume:
        return "Resume Game";
    case Item::ExitGame:
        return "Exit to Library";
    }
    return "";
}

std::string Value(Item item) {
    switch (item) {
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
    default:
        return "";
    }
}

// Left/right on a value row. `dir` is -1 or +1.
void Adjust(Item item, int dir) {
    switch (item) {
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
    default:
        break;
    }
}

// Pressing 'a' on a row advances the list by one.
void Activate(Item item) {
    switch (item) {
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
    default:
        Adjust(item, 1);
        break;
    }
}

void Repaint() {
    VideoCore::OverlayMenuState state;
    state.visible = s_open.load(std::memory_order_relaxed);
    state.title = "Quick Menu";
    state.selected = s_selected;
    state.hint = "A Change   B Back   +/- Close";
    state.items.reserve(kItems.size());
    for (const Item item : kItems) {
        state.items.push_back({Label(item), Value(item), IsAction(item)});
    }
    VideoCore::SetOverlayMenuState(state);
}

} // namespace

bool IsQuickMenuOpen() {
    return s_open.load(std::memory_order_relaxed);
}

void OpenQuickMenu() {
    s_selected = 0;
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

    const int count = static_cast<int>(kItems.size());
    bool changed = false;

    if (nav.cancel) {
        CloseQuickMenu();
        return QuickMenuAction::Close;
    }
    if (nav.up) {
        s_selected = (s_selected - 1 + count) % count;
        changed = true;
    }
    if (nav.down) {
        s_selected = (s_selected + 1) % count;
        changed = true;
    }

    const Item item = kItems[s_selected];
    if (nav.left && !IsAction(item)) {
        Adjust(item, -1);
        changed = true;
    }
    if (nav.right && !IsAction(item)) {
        Adjust(item, 1);
        changed = true;
    }
    if (nav.confirm) {
        if (item == Item::Resume) {
            CloseQuickMenu();
            return QuickMenuAction::Close;
        }
        if (item == Item::ExitGame) {
            CloseQuickMenu();
            return QuickMenuAction::ExitGame;
        }
        Activate(item);
        changed = true;
    }

    if (changed) {
        Repaint();
    }
    return QuickMenuAction::None;
}

} // namespace SwitchFrontend
