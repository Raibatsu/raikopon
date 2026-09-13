// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_game_icons.h"

#include <cstring>
#include <unordered_map>

#include "citra_switch/gametdb.h"

namespace SwitchFrontend {

namespace {

std::vector<AtlasRegion> s_game_icon_regions;
// Keyed by "<product_code>_<cfg_language>" - different languages can have different cached
// covers for the same title, so the language is part of the cache key. Cleared in
// LoadGameIcons() alongside s_game_icon_regions, since both live in the same GpuCanvas atlas and
// that atlas is rebuilt fresh every RunLauncher() session - a region cached against a previous
// atlas is meaningless once that atlas is gone.
std::unordered_map<std::string, CoverArt> s_cover_regions;

} // namespace

void ResetCoverCache() {
    s_cover_regions.clear();
}

void LoadGameIcons(GpuCanvas& canvas, GlyphAtlas& atlas, const std::vector<GameEntry>& games) {
    s_cover_regions.clear();
    s_game_icon_regions.assign(games.size(), AtlasRegion{});
    for (std::size_t i = 0; i < games.size(); ++i) {
        const GameEntry& entry = games[i];
        if (entry.icon_size <= 0 ||
            entry.icon.size() != static_cast<std::size_t>(entry.icon_size * entry.icon_size)) {
            continue;
        }
        const auto size = static_cast<std::uint32_t>(entry.icon_size);
        const AtlasRegion region = atlas.ReserveAtlasRegion(canvas, size, size);
        if (region.u1 <= region.u0) {
            continue;
        }
        canvas.UpdateAtlasRegion(region.x, region.y, size, size,
                                 reinterpret_cast<const std::uint8_t*>(entry.icon.data()));
        s_game_icon_regions[i] = region;
    }
}

AtlasRegion GetGameIconRegion(std::size_t index) {
    if (index >= s_game_icon_regions.size()) {
        return {};
    }
    return s_game_icon_regions[index];
}

std::optional<CoverArt> GetCoverRegion(GpuCanvas& canvas, GlyphAtlas& atlas,
                                       const GameEntry& entry, int cfg_language) {
    if (entry.product_code.empty() || !GetGameTdbEnabled()) {
        return std::nullopt;
    }

    const std::string key = entry.product_code + "_" + std::to_string(cfg_language);
    const auto cached = s_cover_regions.find(key);
    if (cached != s_cover_regions.end()) {
        // A present-but-zero-area entry means a prior attempt failed (decode error, or no atlas
        // space left) - cached as a negative result too, not just successes, so a title whose
        // cover can't be shown doesn't re-run the full disk-read + JPEG-decode pipeline on every
        // single frame for as long as its Game Details screen stays open.
        if (cached->second.region.u1 <= cached->second.region.u0) {
            return std::nullopt;
        }
        return cached->second;
    }

    const std::optional<CoverImage> image = GetCachedCover(entry.product_code, cfg_language);
    if (!image) {
        s_cover_regions[key] = CoverArt{};
        return std::nullopt;
    }
    const auto w = static_cast<std::uint32_t>(image->width);
    const auto h = static_cast<std::uint32_t>(image->height);
    const AtlasRegion region = atlas.ReserveAtlasRegion(canvas, w, h);
    if (region.u1 <= region.u0) {
        s_cover_regions[key] = CoverArt{};
        return std::nullopt;
    }
    canvas.UpdateAtlasRegion(region.x, region.y, w, h,
                             reinterpret_cast<const std::uint8_t*>(image->pixels.data()));
    const CoverArt art{region, image->width, image->height};
    s_cover_regions[key] = art;
    return art;
}

} // namespace SwitchFrontend
