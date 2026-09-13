// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/gpu_canvas.h"

namespace SwitchFrontend {

enum class Icon {
    Library,
    Settings,
    Exit,
    Credits,
    Folder,
    UpDir,
    Count,
};

// Uploads the baked UI icons into the shared atlas via `atlas`'s packer. Must be called once per
// GpuCanvas lifetime (right after GpuCanvas::Init() succeeds), before any GetIconRegion() call -
// the atlas is destroyed and recreated every time the launcher's GpuCanvas is.
void LoadUiIcons(GpuCanvas& canvas, GlyphAtlas& atlas);

// A zero-area region if LoadUiIcons() hasn't run yet (or failed) for the current canvas.
AtlasRegion GetIconRegion(Icon icon);

} // namespace SwitchFrontend
