// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

// Per-game Mods screen, opened from Game Details - lists this title's mod subfolders (see
// library_mods.h) with Confirm toggling each on/off. Unlike Cheats, there's no add/edit/delete
// here - mods are files the user places outside the app, this screen only controls which of them
// get merged in at the title's *next* boot (see ui_paths.h's ApplyRecursivePathsForTitle).
void UpdateModsScreen(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                      float dt);

} // namespace SwitchFrontend
