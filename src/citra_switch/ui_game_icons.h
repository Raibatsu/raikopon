// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/gpu_canvas.h"
#include "citra_switch/menu_data.h"

namespace SwitchFrontend {

// Uploads each game's already-decoded SMDH icon into the shared atlas. Call once after
// ScanGames() populates `games` - re-uploading is cheap relative to the scan itself, and there's
// no persistent GPU resource to leak since GpuCanvas rebuilds its atlas fresh every time
// RunLauncher() runs.
void LoadGameIcons(GpuCanvas& canvas, GlyphAtlas& atlas, const std::vector<GameEntry>& games);

// A zero-area region if LoadGameIcons() hasn't been called yet, or index is out of range.
AtlasRegion GetGameIconRegion(std::size_t index);

void ResetCoverCache();

// AtlasRegion alone doesn't carry pixel dimensions (only UV + texel origin), and covers vary in
// aspect ratio unlike the fixed-size glyph/game-icon regions elsewhere in this atlas - callers
// need width/height to draw it without distortion.
struct CoverArt {
    AtlasRegion region;
    int width = 0;
    int height = 0;
};

// GameTDB cover art for `entry` in `cfg_language` (a Service::CFG::SystemLanguage value, 0-11 -
// see settings_model.cpp's LanguageName), uploaded into the shared atlas the first time it's
// decoded and cached here by (product code, language) thereafter. Pure on-disk-cache lookup, no
// network - see gametdb.h's StartCoverDownload for populating that cache. std::nullopt means
// "nothing to draw" (no product code, GameTDB display disabled, or no cover cached in any
// language for this title).
std::optional<CoverArt> GetCoverRegion(GpuCanvas& canvas, GlyphAtlas& atlas,
                                       const GameEntry& entry, int cfg_language);

} // namespace SwitchFrontend
