// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_layout.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include "citra_switch/game_settings.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"
#include "common/logging/log.h"

namespace SwitchFrontend {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

CanvasColor WithAlpha(CanvasColor color, float alpha_scale) {
    return {color.r, color.g, color.b, color.a * std::clamp(alpha_scale, 0.0f, 1.0f)};
}

void CloseLayoutPreview(UiModel& model) {
    LOG_INFO(Frontend, "ui_layout: closing standalone layout preview");
    EndGameOverrides();
    model.layout_editor_open = false;
}

void DrawPreviewScreen(GpuCanvas& canvas, GlyphAtlas& atlas, const UiPalette& palette,
                       const LayoutEditorPreviewRect& rect, float sx, float sy, bool selected,
                       const std::string& label) {
    const float dx = static_cast<float>(rect.x) * sx;
    const float dy = static_cast<float>(rect.y) * sy;
    const float dw = static_cast<float>(rect.w) * sx;
    const float dh = static_cast<float>(rect.h) * sy;
    const float alpha = static_cast<float>(rect.opacity_percent) / 100.0f;
    const float rotation_radians = static_cast<float>(rect.rotation_degrees) * kDegToRad;

    const float border = selected ? 4.0f : 2.0f;
    const CanvasColor border_color = selected ? palette.accent : palette.text_dim;
    const CanvasColor fill_color = selected ? palette.accent_dim : palette.surface_warm;

    canvas.DrawQuad(dx, dy, dw, dh, border_color, rotation_radians);
    canvas.DrawQuad(dx + border, dy + border, dw - border * 2.0f, dh - border * 2.0f,
                    WithAlpha(fill_color, alpha), rotation_radians);

    const float label_w = MeasureText(atlas, canvas, label);
    const float label_y = dy + dh * 0.5f - atlas.LineHeight();
    DrawText(canvas, atlas, dx + (dw - label_w) * 0.5f, label_y, label,
            selected ? palette.text : palette.text_dim);

    const std::string opacity_text =
        fmt::format("{}{}%", Tr("layout.opacity_prefix"), rect.opacity_percent);
    const float opacity_w = MeasureText(atlas, canvas, opacity_text);
    DrawText(canvas, atlas, dx + (dw - opacity_w) * 0.5f, label_y + atlas.LineHeight(),
            opacity_text, selected ? palette.text : palette.text_dim);

    const std::string rotation_text =
        fmt::format("{}{}°", Tr("layout.rotation_prefix"), rect.rotation_degrees);
    const float rotation_w = MeasureText(atlas, canvas, rotation_text);
    DrawText(canvas, atlas, dx + (dw - rotation_w) * 0.5f, label_y + atlas.LineHeight() * 2.0f,
            rotation_text, selected ? palette.text : palette.text_dim);
}

} // namespace

void UpdateLayoutPreview(UiModel& model, const InputState& raw_input, const LayoutEditorNav& nav,
                         GpuCanvas& canvas, GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    if (!IsLayoutEditorStandaloneOpen()) {
        LOG_WARNING(Frontend, "ui_layout: reached with no standalone session active, closing");
        CloseLayoutPreview(model);
        return;
    }

    UpdateLayoutEditor(raw_input, nav);

    if (!IsLayoutEditorStandaloneOpen()) {
        CloseLayoutPreview(model);
        return;
    }

    const LayoutEditorPreview state = GetLayoutEditorPreview();
    if (!state.visible || state.canvas_width <= 0 || state.canvas_height <= 0) {
        LOG_WARNING(Frontend, "ui_layout: preview state not visible or malformed (w={}, h={})",
                    state.canvas_width, state.canvas_height);
        return;
    }

    const float sx = screen_w / static_cast<float>(state.canvas_width);
    const float sy = screen_h / static_cast<float>(state.canvas_height);

    // Whichever draws last visually wins where the two rects overlap, matching the real
    // in-game/DrawScreens draw order for CustomLayout.
    if (state.top_on_top) {
        DrawPreviewScreen(canvas, atlas, palette, state.bottom, sx, sy, state.selected_bottom,
                          Tr("layout.bottom_screen"));
        DrawPreviewScreen(canvas, atlas, palette, state.top, sx, sy, state.selected_top,
                          Tr("layout.top_screen"));
    } else {
        DrawPreviewScreen(canvas, atlas, palette, state.top, sx, sy, state.selected_top,
                          Tr("layout.top_screen"));
        DrawPreviewScreen(canvas, atlas, palette, state.bottom, sx, sy, state.selected_bottom,
                          Tr("layout.bottom_screen"));
    }

    const std::string legend =
        state.rotation_mode ? Tr("layout.hint.rotating") : Tr("layout.hint.controls");
    const float legend_w = MeasureText(atlas, canvas, legend);
    DrawText(canvas, atlas, (screen_w - legend_w) * 0.5f,
            screen_h - kHintBarHeight - kContentPadding - atlas.LineHeight(), legend,
            state.rotation_mode ? palette.accent : palette.text_dim);

    model.hint_chips[0] = {"A", Tr("layout.action.save")};
    model.hint_chips[1] = {"B", Tr("hint.cancel")};
    model.hint_chips[2] = {"X", state.aspect_locked ? Tr("layout.action.free_stretch") : Tr("layout.action.lock_aspect")};
    model.hint_chips[3] = {"-", Tr("hint.reset")};
    model.hint_chips[4] = {"Y", state.rotation_mode ? Tr("layout.action.stop_rotating") : Tr("layout.action.rotate")};
    model.hint_chip_count = 5;
}

} // namespace SwitchFrontend
