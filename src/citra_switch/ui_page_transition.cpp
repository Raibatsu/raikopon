// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_page_transition.h"

#include <algorithm>
#include <cmath>

#include "citra_switch/ui_gamedetails.h"
#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_library.h"
#include "citra_switch/ui_navbar.h"
#include "citra_switch/ui_settings.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kTransitionDuration = 0.28f;
constexpr float kSlideDistance = 50.0f;
constexpr float kScaleDelta = 0.08f;
constexpr float kTiltRadians = 4.0f * 3.14159265f / 180.0f;

std::uint32_t SlotFor(RailItem rail) {
    return rail == RailItem::Settings ? 1u : 0u;
}

float EaseInOutCubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }
    const float f = -2.0f * t + 2.0f;
    return 1.0f - (f * f * f) * 0.5f;
}

void DispatchUpdate(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas) {
    if (model.library_panel == LibraryPanel::Details && model.active_rail == RailItem::Library) {
        UpdateGameDetails(model, input, canvas, atlas);
    } else if (model.active_rail == RailItem::Settings) {
        UpdateSettings(model, input, canvas, atlas, 0.0f);
    } else {
        UpdateLibrary(model, input, canvas, atlas, 0.0f);
    }
}

void DrawStaticRail(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas) {
    const float screen_h = static_cast<float>(canvas.Height());
    const NavItem rail_items[] = {{Tr("nav.library"), GetIconRegion(Icon::Library)},
                                  {Tr("nav.settings"), GetIconRegion(Icon::Settings)},
                                  {Tr("nav.credits"), GetIconRegion(Icon::Credits)},
                                  {Tr("nav.exit"), GetIconRegion(Icon::Exit)}};
    DrawNavBar(canvas, atlas, rail_items, kNumRailItems, static_cast<int>(model.active_rail), 0.0f,
              screen_h * 0.5f, kRailCollapsedWidth, kRailItemHeight, true, MenuInput{},
              model.wave_elapsed);
    DrawNavBarActiveTab(canvas, atlas, rail_items, kNumRailItems,
                        static_cast<int>(model.active_rail), 0.0f, screen_h * 0.5f,
                        kRailCollapsedWidth, kRailItemHeight, model.rail_pop_anim);
}

} // namespace

void TriggerPageTransition(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas, RailItem to,
                           bool commit) {
    if (model.page_transition_active) {
        return;
    }
    if (to != RailItem::Library && to != RailItem::Settings) {
        return;
    }
    if (model.active_rail == RailItem::Credits) {
        return;
    }
    if (model.active_rail == to) {
        return;
    }

    const RailItem from = model.active_rail;
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    const float content_x = kRailCollapsedWidth;
    const float content_w = std::max(0.0f, screen_w - content_x);

    canvas.BeginCapture(SlotFor(from), palette.bg);
    canvas.SetClipRect(content_x, 0.0f, content_w, screen_h);
    DispatchUpdate(model, MenuInput{}, canvas, atlas);
    canvas.ClearClipRect();
    canvas.EndCapture();

    model.active_rail = to;
    model.library_panel = LibraryPanel::List;
    if (commit) {
        model.focus = LibraryFocus::List;
        model.rail_focused = false;
    }

    canvas.BeginCapture(SlotFor(to), palette.bg);
    canvas.SetClipRect(content_x, 0.0f, content_w, screen_h);
    DispatchUpdate(model, MenuInput{}, canvas, atlas);
    canvas.ClearClipRect();
    canvas.EndCapture();

    model.page_transition_hint_chips = model.hint_chips;
    model.page_transition_hint_chip_count = model.hint_chip_count;

    model.page_transition_active = true;
    model.page_transition_t = 0.0f;
    model.page_transition_from = from;
    model.page_transition_to = to;

    UpdatePageTransition(model, canvas, atlas, 0.0f);
}

void UpdatePageTransition(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas, float dt) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    model.page_transition_t =
        std::min(1.0f, model.page_transition_t + dt / kTransitionDuration);
    const float eased = EaseInOutCubic(model.page_transition_t);

    const float content_x = kRailCollapsedWidth;
    const float content_w = std::max(0.0f, screen_w - content_x);
    const float content_h = screen_h;

    const float out_scale = 1.0f - kScaleDelta * eased;
    const float out_w = content_w * out_scale;
    const float out_h = content_h * out_scale;
    const float out_x = content_x + (content_w - out_w) * 0.5f;
    const float out_y = (content_h - out_h) * 0.5f + kSlideDistance * eased;
    canvas.DrawCaptureQuad(SlotFor(model.page_transition_from), out_x, out_y, out_w, out_h,
                           -kTiltRadians * eased, content_x, 0.0f, content_w, content_h);

    const float in_scale = (1.0f - kScaleDelta) + kScaleDelta * eased;
    const float in_w = content_w * in_scale;
    const float in_h = content_h * in_scale;
    const float in_x = content_x + (content_w - in_w) * 0.5f;
    const float in_y = (content_h - in_h) * 0.5f + kSlideDistance * (1.0f - eased);
    canvas.DrawCaptureQuad(SlotFor(model.page_transition_to), in_x, in_y, in_w, in_h,
                           kTiltRadians * (1.0f - eased), content_x, 0.0f, content_w, content_h);

    DrawStaticRail(model, canvas, atlas);

    model.hint_chips = model.page_transition_hint_chips;
    model.hint_chip_count = model.page_transition_hint_chip_count;

    if (model.page_transition_t >= 1.0f) {
        model.page_transition_active = false;
    }
}

} // namespace SwitchFrontend
