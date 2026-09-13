// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

// Draws the persistent bottom hint bar. Called once per frame from the launcher's own loop,
// after whichever screen just drew, so it always ends up as the top-most layer and looks
// identical across every screen instead of being drawn (and subtly re-implemented) per screen.
// Badge assets are shared with the Controls tab's binding chips - see ui_badge.h's
// LoadBadgeAssets(), called once alongside LoadUiIcons() in citra_switch.cpp.
void DrawHintBar(GpuCanvas& canvas, GlyphAtlas& atlas, const HintChip* chips, int count);

} // namespace SwitchFrontend
