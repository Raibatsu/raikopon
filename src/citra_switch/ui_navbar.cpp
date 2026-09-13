// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_navbar.h"

#include <cmath>

#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {
constexpr float kActiveTiltBase = -0.0698f;
constexpr float kActiveTiltSwing = 0.035f;
constexpr float kActiveTiltSpeed = 1.5f;
constexpr float kIconSize = 28.0f;
constexpr float kActiveTabPaddingLeft = 16.0f;
constexpr float kActiveTabPaddingRight = 20.0f;
} // namespace

int DrawNavBar(GpuCanvas& canvas, GlyphAtlas& atlas, const NavItem* items, int count,
               int active_index, float origin_x, float origin_y, float bar_thickness,
               float item_length, bool vertical, const MenuInput& input, float time_seconds) {
    const UiPalette& palette = CurrentPalette();
    const float total_length = item_length * static_cast<float>(count);
    const float bar_w = vertical ? bar_thickness : total_length;
    const float bar_h = vertical ? total_length : bar_thickness;
    // Only vertical bars center on origin_y - horizontal bars (the Settings tab strip) keep
    // origin_x as their literal left edge, same as before centering was added for the rail.
    const float start_x = origin_x;
    const float start_y = vertical ? origin_y - total_length * 0.5f : origin_y;
    canvas.DrawQuad(start_x, start_y, bar_w, bar_h, palette.surface);

    const float tilt =
        kActiveTiltBase + std::sin(time_seconds * kActiveTiltSpeed) * kActiveTiltSwing;

    int tapped = -1;
    for (int i = 0; i < count; ++i) {
        const float item_x = vertical ? start_x : start_x + item_length * static_cast<float>(i);
        const float item_y = vertical ? start_y + item_length * static_cast<float>(i) : start_y;
        const float item_w = vertical ? bar_thickness : item_length;
        const float item_h = vertical ? item_length : bar_thickness;

        if (input.touch_tap && input.touch_x >= item_x && input.touch_x < item_x + item_w &&
            input.touch_y >= item_y && input.touch_y < item_y + item_h) {
            tapped = i;
        }

        const bool active = i == active_index;
        if (active) {
            // Rail items stay flat (no tilt) - it now sits right next to the active item's flat
            // bookmark tab (see DrawNavBarActiveTab), and a wobbling icon cell next to a rigid tab
            // read as disjointed rather than lively. The horizontal Settings tab strip keeps the
            // wobble - it has no tab overlay of its own to clash with.
            canvas.DrawQuad(item_x, item_y, item_w, item_h, palette.accent, vertical ? 0.0f : tilt);
        }

        const CanvasColor label_color = active ? palette.bg : palette.text;
        const AtlasRegion& icon = items[i].icon;
        const bool has_icon = vertical && icon.u1 > icon.u0;

        if (has_icon) {
            const float icon_x = item_x + (item_w - kIconSize) * 0.5f;
            const float icon_y = item_y + (item_h - kIconSize) * 0.5f;
            canvas.DrawTexturedQuad(icon_x, icon_y, kIconSize, kIconSize, icon.u0, icon.v0,
                                    icon.u1, icon.v1, label_color);
        } else {
            const float text_width = MeasureText(atlas, canvas, items[i].label);
            const float text_x = item_x + (item_w - text_width) * 0.5f;
            const float text_y = item_y + (item_h - atlas.LineHeight()) * 0.5f;
            DrawText(canvas, atlas, text_x, text_y, items[i].label, label_color);
        }
    }
    return tapped;
}

void DrawNavBarActiveTab(GpuCanvas& canvas, GlyphAtlas& atlas, const NavItem* items, int count,
                         int active_index, float origin_x, float origin_y, float bar_thickness,
                         float item_length, float pop_amount) {
    if (pop_amount < 0.5f || active_index < 0 || active_index >= count) {
        return;
    }
    const UiPalette& palette = CurrentPalette();
    const float total_length = item_length * static_cast<float>(count);
    const float start_y = origin_y - total_length * 0.5f;
    const float item_y = start_y + item_length * static_cast<float>(active_index);
    const float tab_x = origin_x + bar_thickness;

    canvas.DrawQuad(tab_x, item_y, pop_amount, item_length, palette.accent);

    const std::string& label = items[active_index].label;
    const float text_x = tab_x + kActiveTabPaddingLeft;
    const float text_y = item_y + (item_length - atlas.LineHeight()) * 0.5f;
    DrawText(canvas, atlas, text_x, text_y, label, palette.bg);
}

float NavBarActiveTabWidth(GpuCanvas& canvas, GlyphAtlas& atlas, const std::string& label) {
    return MeasureText(atlas, canvas, label) + kActiveTabPaddingLeft + kActiveTabPaddingRight;
}

} // namespace SwitchFrontend
