// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/gpu_canvas.h"
#include "citra_switch/ui_model.h"

namespace SwitchFrontend {

struct NavItem {
    // std::string, not const char*, since labels are usually the (per-frame, already-localized)
    // result of a Tr() call - see ui_strings.h - not a string literal.
    std::string label;
    AtlasRegion icon{}; // Default (u1<=u0) means no icon - label-only.
};

// Draws the bar and returns the index of whichever item `input.touch_tap` landed on this frame,
// or -1 if none. For a vertical bar, origin_y is the vertical CENTER of the whole item stack (not
// its top) - the stack is centered around it rather than growing down from it. time_seconds drives
// the active item's tilt wobble (a slow sinusoidal rock around the base angle, shared across every
// bar so they all stay in phase). Vertical bars with an icon always draw icon-only, centered, no
// label - the active item's label appears separately via DrawNavBarActiveTab instead.
int DrawNavBar(GpuCanvas& canvas, GlyphAtlas& atlas, const NavItem* items, int count,
               int active_index, float origin_x, float origin_y, float bar_thickness,
               float item_length, bool vertical, const MenuInput& input, float time_seconds);

// Draws the active item's "bookmark tab": a label-bearing flag that pops out past the end of a
// vertical bar by `pop_amount` pixels, instead of the whole bar widening and pushing screen
// content out of the way. No-op if pop_amount is negligible or active_index is out of range. Call
// this AFTER the screen's own content has been drawn, so the tab overlays on top of it rather
// than content drawing over it.
void DrawNavBarActiveTab(GpuCanvas& canvas, GlyphAtlas& atlas, const NavItem* items, int count,
                         int active_index, float origin_x, float origin_y, float bar_thickness,
                         float item_length, float pop_amount);

float NavBarActiveTabWidth(GpuCanvas& canvas, GlyphAtlas& atlas, const std::string& label);

// RAII wrapper around DrawNavBarActiveTab: construct it right after the base DrawNavBar() call
// (once you know rail navigation didn't jump to a different screen this frame), and it draws the
// tab when it goes out of scope. Screens with several early returns after that point (Library,
// Settings, Game Details all have a handful) would otherwise need the same DrawNavBarActiveTab
// call repeated at every one of them - easy to miss on a future edit - so this guarantees the tab
// always draws last, on top of whatever content the caller ends up drawing before returning.
class NavBarActiveTabGuard {
public:
    NavBarActiveTabGuard(GpuCanvas& canvas, GlyphAtlas& atlas, const NavItem* items, int count,
                         int active_index, float origin_x, float origin_y, float bar_thickness,
                         float item_length, float pop_amount)
        : canvas_(canvas), atlas_(atlas), items_(items), count_(count),
          active_index_(active_index), origin_x_(origin_x), origin_y_(origin_y),
          bar_thickness_(bar_thickness), item_length_(item_length), pop_amount_(pop_amount) {}
    NavBarActiveTabGuard(const NavBarActiveTabGuard&) = delete;
    NavBarActiveTabGuard& operator=(const NavBarActiveTabGuard&) = delete;
    ~NavBarActiveTabGuard() {
        DrawNavBarActiveTab(canvas_, atlas_, items_, count_, active_index_, origin_x_, origin_y_,
                            bar_thickness_, item_length_, pop_amount_);
    }

private:
    GpuCanvas& canvas_;
    GlyphAtlas& atlas_;
    const NavItem* items_;
    int count_;
    int active_index_;
    float origin_x_;
    float origin_y_;
    float bar_thickness_;
    float item_length_;
    float pop_amount_;
};

} // namespace SwitchFrontend
