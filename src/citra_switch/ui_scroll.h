// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

// The vertical offset (>=0) to subtract from every row's unscrolled Y position so that a
// selected row spanning [selected_top, selected_top + selected_height) stays fully inside
// [viewport_top, viewport_bottom). Pure function of the selection - no persisted scroll state.
float ComputeScrollOffset(float selected_top, float selected_height, float viewport_top,
                          float viewport_bottom);

// Drives state.offset for one scrollable list, combining touch-drag and selection-follow into a
// single call. While a drag owns `state` (claimed when a press starts inside the viewport rect),
// the offset rides the finger, clamped to [0, max_scroll]. Otherwise the offset only gets
// resynced via ComputeScrollOffset() when `selection` has changed since the last call (or on the
// very first call) - so releasing a touch-scroll leaves the list wherever it was dragged to
// instead of snapping back to the selected row every frame.
void UpdateScroll(ScrollState& state, const MenuInput& input, int selection, float selected_top,
                  float selected_height, float viewport_x, float viewport_y, float viewport_w,
                  float viewport_h, float max_scroll);

} // namespace SwitchFrontend
