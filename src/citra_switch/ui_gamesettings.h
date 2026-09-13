// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

void UpdateGameSettings(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                        GlyphAtlas& atlas, float dt);

} // namespace SwitchFrontend
