// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

// The Controls tab's content area (rail + Settings tab strip are already drawn by the caller).
// Two clearly separated sections in one scrollable list: "In-Game Controls" (single-button rebind
// via the existing input.h mapping API, unchanged) and "UI Controls" (multi-button rebind via the
// existing ui_input_bindings.h menu-action API, unchanged) - both backends are untouched, this is
// purely a new UI layer over them.
void UpdateControlsTab(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                       GlyphAtlas& atlas, float dt, float content_x, float content_top,
                       float content_w, float viewport_bottom);

// Resets every In-Game Control and UI Control binding to its default, and persists both.
void ResetAllControlsToDefault();

} // namespace SwitchFrontend
