// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_scroll.h"

#include <algorithm>

namespace SwitchFrontend {

void UpdateScroll(ScrollState& state, const MenuInput& input, int selection, float selected_top,
                  float selected_height, float viewport_x, float viewport_y, float viewport_w,
                  float viewport_h, float max_scroll) {
    const float clamped_max = std::max(0.0f, max_scroll);
    const bool within_viewport = input.touch_held_x >= viewport_x &&
                                 input.touch_held_x < viewport_x + viewport_w &&
                                 input.touch_held_y >= viewport_y &&
                                 input.touch_held_y < viewport_y + viewport_h;
    if (input.touch_press_edge && within_viewport) {
        state.dragging = true;
    }
    if (state.dragging && !input.touch_held) {
        state.dragging = false;
    }

    if (state.dragging) {
        state.offset = std::clamp(state.offset - input.touch_delta_y, 0.0f, clamped_max);
        state.last_selection = selection;
        return;
    }

    if (state.last_selection != selection) {
        state.offset =
            ComputeScrollOffset(selected_top, selected_height, viewport_y, viewport_y + viewport_h);
        state.last_selection = selection;
    } else {
        state.offset = std::clamp(state.offset, 0.0f, clamped_max);
    }
}

float ComputeScrollOffset(float selected_top, float selected_height, float viewport_top,
                          float viewport_bottom) {
    const float selected_bottom = selected_top + selected_height;
    const float viewport_height = viewport_bottom - viewport_top;
    if (selected_height >= viewport_height) {
        return std::max(0.0f, selected_top - viewport_top);
    }

    float offset = 0.0f;
    if (selected_bottom - offset > viewport_bottom) {
        offset = selected_bottom - viewport_bottom;
    }
    if (selected_top - offset < viewport_top) {
        offset = selected_top - viewport_top;
    }
    return std::max(0.0f, offset);
}

} // namespace SwitchFrontend
