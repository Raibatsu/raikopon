// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

// The Paths tab's content area (rail + Settings tab strip are already drawn by the caller).
// Two rows - Library Folder and SD Card Folder - each opening a shared folder-browse overlay
// (ListSubdirectories/ParentDirectory, same primitives ui_install.cpp's CIA browser already
// uses) rooted at the row's current value; confirm on Minus inside that overlay applies the
// currently-browsed directory.
void UpdatePathsTab(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                    float dt, float content_x, float content_top, float content_w,
                    float viewport_bottom);

// Applies the persisted SD Card Folder override (if any) to FileUtil::UserPath::SDMCDir. Call
// once at boot, right after SwitchFrontend::Bootstrap() - before any game scan or boot touches
// the SD card. No-op if no override was ever set, or the saved path is no longer a real
// directory (e.g. an SD card swap since it was picked).
void ApplySavedStorageOverride();

// If the Paths tab's "include subfolders" toggle is on for Cheats, searches under the current
// Cheats folder for this title's cheat file nested inside a subfolder and, if found somewhere
// other than the top level, temporarily redirects FileUtil::UserPath::CheatsDir to it so core's
// fixed "<dir>{title_id}.txt" lookup resolves there. Leaves the dir alone if nothing matches
// anywhere (a brand-new cheat then still saves to the top level).
//
// For Mods: resolves this title's mods/{title_id}/ folder the same way (if the toggle is on),
// then checks whether it contains named subfolders - see DiscoverTitleMods() below. If it does,
// each enabled one (GetDisabledMods()) is merged into a staging copy and
// FileUtil::UserPath::LoadDir is redirected there instead, so core loads only the enabled subset.
// If there are no subfolders (flat/legacy mods, or none at all), just redirects LoadDir like
// Cheats does above - no merge, no staging, existing setups are unaffected.
//
// Call right before booting a title; pair with RestorePathsAfterGame() once that session ends.
void ApplyRecursivePathsForTitle(std::uint64_t title_id);

// Undoes ApplyRecursivePathsForTitle(), restoring whatever Cheats/Mods folder was active right
// before it ran. Call once a game session ends, before the next title (or the library) reads
// from these paths. Safe to call even if ApplyRecursivePathsForTitle() never redirected anything.
void RestorePathsAfterGame();

// Names of the subfolders directly under this title's mods/{title_id}/ folder (resolved the same
// way ApplyRecursivePathsForTitle() resolves it, honoring the Mods "include subfolders" toggle).
// Each one is treated as an individually-toggleable mod by the Game Details Mods screen - empty
// if the title has no mods folder, or its mods aren't organized into named subfolders (those
// still load normally at boot, as a single unit, just like before this existed).
std::vector<std::string> DiscoverTitleMods(std::uint64_t title_id);

// Reads/writes which of DiscoverTitleMods()'s entries are disabled for this title, persisted at
// {ConfigDir}mod_toggles/{title_id}.txt - one name per line, no file (or an empty one) means
// everything is enabled.
std::vector<std::string> GetDisabledMods(std::uint64_t title_id);
void SetDisabledMods(std::uint64_t title_id, const std::vector<std::string>& disabled);

} // namespace SwitchFrontend
