// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

// A render-agnostic description of an on-screen overlay menu. The frontend fills it in from the
// input thread and the active renderer reads it back on the emulation thread to draw it on top of
// the game.
namespace VideoCore {

// One line in the overlay. Actions (Resume, Exit, etc.) carry no value.
struct OverlayMenuItem {
    std::string label;
    std::string value;
    bool is_action{};
};

struct OverlayMenuState {
    bool visible{};
    std::string title;
    std::vector<OverlayMenuItem> items;
    int selected{};
    bool armed{}; // True while the selected row is armed for joystick left/right adjustment.
    std::string hint; // Footer help text.
};

// Publishes the latest overlay description (called from the input thread).
void SetOverlayMenuState(const OverlayMenuState& state);

// Snapshots the current overlay description (called from the renderer/emulation thread).
OverlayMenuState GetOverlayMenuState();

// Skip the copy above when nothing is shown.
bool IsOverlayMenuVisible();

} // namespace VideoCore
