// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// Plain-type facade over Core::System's CheatEngine, so the in-game settings screen (which owns
// a native libnx Framebuffer/PadState and therefore can't include <switch.h> in the same
// translation unit as core headers — see ReleaseWindowForMenu's comment in emulation.cpp) can
// drive cheats without touching core/cheats/* directly. Same CheatEngine calls the old Vulkan
// quick menu's cheat tab used; only the host UI changed.
namespace SwitchFrontend {

int CheatCount();
std::string CheatName(int index);
bool CheatEnabled(int index);
void ToggleCheat(int index);

// Writes the enabled state (and any add/edit/delete below) back to the cheat file if anything
// changed since the last call. Safe to call unconditionally.
void PersistCheats();

// Prompts for a name then the Gateway-format code one line at a time via the system keyboard.
// `edit_index` >= 0 modifies that existing cheat (fields pre-filled); -1 creates a new one
// (starts disabled). Returns the index the caller should select afterward, or -1 if the flow was
// cancelled or produced nothing (in which case the caller's current selection is left as-is).
int EditCheatFlow(int edit_index);

// Deletes cheat `index`. Returns the index the caller should select afterward.
int DeleteCheatFlow(int index);

} // namespace SwitchFrontend
