// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>

// File-based cheat editing for a title, usable pre-boot (Library/Game Details) where no
// Core::System is running yet and ingame_cheats.h's live CheatEngine doesn't exist. Operates
// directly on the same GatewayCheat .txt file the live engine reads/writes
// (FileUtil::UserPath::CheatsDir + "<title_id>.txt"), keyed explicitly by title_id rather than
// "whatever's currently running".
namespace SwitchFrontend {

// Loads (or reloads, if a different title_id than last time) the cheat file for `title_id` into
// memory. Safe to call every frame - a no-op once already loaded for this title_id.
void LoadLibraryCheats(std::uint64_t title_id);

int LibraryCheatCount();
std::string LibraryCheatName(int index);
bool LibraryCheatEnabled(int index);
void ToggleLibraryCheat(int index);

// Writes the loaded list back to the cheat file if anything changed since the last call. Safe to
// call unconditionally.
void PersistLibraryCheats();

// Same shape as ingame_cheats.h's EditCheatFlow/DeleteCheatFlow - see there for the exact
// contract (edit_index >= 0 modifies, -1 creates; both return the index the caller should select
// afterward, or -1/unchanged if cancelled).
int EditLibraryCheatFlow(int edit_index);
int DeleteLibraryCheatFlow(int index);

} // namespace SwitchFrontend
