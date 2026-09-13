// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/gpu_canvas.h"

namespace SwitchFrontend {

// Bakes the circular badge mask shared by every colored button badge in the app (hint bar, the
// Controls tab's per-row binding chips, ...) into the atlas. Call once per GpuCanvas lifetime
// (right after GpuCanvas::Init()), before the first DrawButtonBadge() call for that canvas.
void LoadBadgeAssets(GpuCanvas& canvas, GlyphAtlas& atlas);

// Real Nintendo Switch face-button colors for "A"/"B"/"X"/"Y" (case-sensitive), a neutral dark
// badge for everything else (L/R/ZL/ZR/+/-/...) - those aren't color-coded on real hardware
// either, so a neutral badge is the accurate choice, not a placeholder.
CanvasColor ButtonBadgeColor(const char* label);

// Draws one circular badge (dark outline ring + colored fill + centered white label) at `size`
// diameter, vertically centered on `center_y`, left edge at `x`. No-op if LoadBadgeAssets() hasn't
// run yet for this canvas.
void DrawButtonBadge(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float center_y, float size,
                     const char* label);

} // namespace SwitchFrontend
