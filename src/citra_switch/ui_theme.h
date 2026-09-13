// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/gpu_canvas.h"

namespace SwitchFrontend {

struct UiPalette {
    CanvasColor bg;
    CanvasColor surface;
    CanvasColor surface_warm;
    CanvasColor accent;
    CanvasColor accent_dim;
    CanvasColor text;
    CanvasColor text_dim;
};

const UiPalette& DarkPalette();
const UiPalette& LightPalette();
const UiPalette& CurrentPalette();

// The Library rail's width - fixed, never animates. Just enough for a centered icon, no label;
// the active item's label appears on a separate "bookmark tab" that pops out over the content
// instead (see kRailPopWidth), rather than the whole rail widening and pushing content over.
inline constexpr float kRailCollapsedWidth = 72.0f;
// How far the active rail item's bookmark tab pops out past kRailCollapsedWidth while the rail
// has focus - matches the old expanded-rail width's reach so the label has the same room it used
// to get when the whole bar widened.
inline constexpr float kRailPopWidth = 88.0f;
inline constexpr float kRailItemHeight = 72.0f;
inline constexpr float kContentPadding = 24.0f;
// Content's left edge, fixed regardless of rail focus - the rail's bookmark tab overlays on top
// of content instead of pushing it over, so this never needs to account for rail_pop_anim.
inline constexpr float kRailContentX = kRailCollapsedWidth + kContentPadding;
inline constexpr float kSelectedGrowth = 1.2f;
inline constexpr float kBannerHeight = 40.0f;
inline constexpr float kHintBarHeight = 44.0f;
inline constexpr float kHeaderUnderlineHeight = 2.0f;

inline float EaseOutCubic(float t) {
    const float f = t - 1.0f;
    return f * f * f + 1.0f;
}

} // namespace SwitchFrontend
