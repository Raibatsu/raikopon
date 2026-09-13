// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace SwitchFrontend {

// Loads this title's mod subfolder list (ui_paths.h's DiscoverTitleMods) plus its persisted
// enable/disable state - call before using ModCount()/ModName()/ModEnabled(). A no-op if this
// title is already loaded, mirroring library_cheats.h's LoadLibraryCheats.
void LoadTitleMods(std::uint64_t title_id);

int ModCount();
std::string ModName(int index);
bool ModEnabled(int index);
void ToggleMod(int index);

// Persists the current enable/disable state to disk - call when leaving the Mods screen. Only
// this frontend's staged merge for the *next* boot is affected; a currently-running game already
// loaded its RomFS/ExeFS overrides and won't see the change until it's restarted.
void PersistTitleMods();

} // namespace SwitchFrontend
