// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_hintbar.h"

#include "citra_switch/ui_badge.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kBadgeSize = 32.0f;
constexpr float kBadgeLabelGap = 8.0f;
constexpr float kChipGap = 24.0f;
constexpr float kSidePadding = 20.0f;

} // namespace

void DrawHintBar(GpuCanvas& canvas, GlyphAtlas& atlas, const HintChip* chips, int count) {
    if (count <= 0) {
        return;
    }

    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    const float bar_y = screen_h - kHintBarHeight;
    canvas.DrawQuad(0.0f, bar_y, screen_w, kHintBarHeight, palette.surface);

    const float center_y = bar_y + kHintBarHeight * 0.5f;
    float x = kSidePadding;
    for (int i = 0; i < count; ++i) {
        const HintChip& chip = chips[i];
        if (chip.button[0] == '\0') {
            continue;
        }

        DrawButtonBadge(canvas, atlas, x, center_y, kBadgeSize, chip.button);
        x += kBadgeSize + kBadgeLabelGap;

        const float label_width = MeasureText(atlas, canvas, chip.label);
        DrawText(canvas, atlas, x, center_y - atlas.LineHeight() * 0.5f, chip.label,
                palette.text_dim);
        x += label_width + kChipGap;
    }
}

} // namespace SwitchFrontend
