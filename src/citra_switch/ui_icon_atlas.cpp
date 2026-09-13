// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_icon_atlas.h"

#include <array>

#include "citra_switch/ui_icons.h"
#include "citra_switch/ui_paths_icons.h"

namespace SwitchFrontend {

namespace {

constexpr int kNumIcons = static_cast<int>(Icon::Count);
std::array<AtlasRegion, kNumIcons> s_icon_regions{};

void LoadOne(GpuCanvas& canvas, GlyphAtlas& atlas, Icon icon, const std::uint8_t* pixels) {
    const AtlasRegion region =
        atlas.ReserveAtlasRegion(canvas, static_cast<std::uint32_t>(UiIcons::kSize),
                                 static_cast<std::uint32_t>(UiIcons::kSize));
    if (region.u1 > region.u0) {
        constexpr std::size_t kPixelCount = UiIcons::kSize * UiIcons::kSize;
        std::array<std::uint8_t, kPixelCount * 4> rgba{};
        for (std::size_t i = 0; i < kPixelCount; ++i) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = pixels[i];
        }
        canvas.UpdateAtlasRegion(region.x, region.y, static_cast<std::uint32_t>(UiIcons::kSize),
                                 static_cast<std::uint32_t>(UiIcons::kSize), rgba.data());
    }
    s_icon_regions[static_cast<int>(icon)] = region;
}

} // namespace

void LoadUiIcons(GpuCanvas& canvas, GlyphAtlas& atlas) {
    LoadOne(canvas, atlas, Icon::Library, UiIcons::kLibrary);
    LoadOne(canvas, atlas, Icon::Settings, UiIcons::kSettings);
    LoadOne(canvas, atlas, Icon::Exit, UiIcons::kExit);
    LoadOne(canvas, atlas, Icon::Credits, UiIcons::kCredits);
    LoadOne(canvas, atlas, Icon::Folder, UiPathsIcons::kFolder);
    LoadOne(canvas, atlas, Icon::UpDir, UiPathsIcons::kUpDir);
}

AtlasRegion GetIconRegion(Icon icon) {
    return s_icon_regions[static_cast<int>(icon)];
}

} // namespace SwitchFrontend
