// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/input.h"
#include "citra_switch/layout_editor.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

void UpdateLayoutPreview(UiModel& model, const InputState& raw_input, const LayoutEditorNav& nav,
                         GpuCanvas& canvas, GlyphAtlas& atlas);

} // namespace SwitchFrontend
