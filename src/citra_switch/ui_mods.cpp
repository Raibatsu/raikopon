// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_mods.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "citra_switch/library_mods.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 48.0f;
constexpr float kRowPadding = 6.0f;

} // namespace

void UpdateModsScreen(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                      float dt) {
    (void)dt;
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const float content_x = kContentPadding;
    const float row_w = screen_w - kContentPadding * 2.0f;
    float y = kContentPadding;

    const std::string title =
        model.mods_title.empty() ? Tr("gamedetails.action.mods") : model.mods_title;
    DrawText(canvas, atlas, content_x, y, title, palette.text);
    y += atlas.LineHeight() + kContentPadding * 0.5f;

    const int row_count = ModCount();

    if (row_count > 0) {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("mods.toggle")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chip_count = 2;
    } else {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chip_count = 1;
    }

    if (input.cancel) {
        PersistTitleMods();
        model.mods_open = false;
        return;
    }

    if (row_count == 0) {
        for (const std::string& line : WrapText(canvas, atlas, Tr("mods.empty"), row_w)) {
            DrawText(canvas, atlas, content_x, y, line, palette.text_dim);
            y += atlas.LineHeight();
        }
        return;
    }

    if (model.mods_row >= row_count) {
        model.mods_row = row_count - 1;
    }

    if (input.up && model.mods_row > 0) {
        --model.mods_row;
    }
    if (input.down && model.mods_row + 1 < row_count) {
        ++model.mods_row;
    }
    if (input.confirm) {
        ToggleMod(model.mods_row);
    }

    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;
    canvas.SetClipRect(content_x, y, row_w, viewport_bottom - y);
    float row_y = y;
    for (int i = 0; i < row_count; ++i) {
        const bool selected = i == model.mods_row;
        const float row_h = selected ? kRowHeight * kSelectedGrowth : kRowHeight;
        if (row_y + row_h >= y && row_y <= viewport_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h, palette.surface_warm);
            }
            const float label_y = row_y + (row_h - atlas.LineHeight()) * 0.5f;
            DrawText(canvas, atlas, content_x + kContentPadding, label_y, ModName(i),
                    selected ? palette.text : palette.text_dim);
            const std::string value = ModEnabled(i) ? Tr("common.on") : Tr("common.off");
            const float value_width = MeasureText(atlas, canvas, value);
            DrawText(canvas, atlas, content_x + row_w - kContentPadding - value_width, label_y,
                    value, selected ? palette.accent : palette.text_dim);
        }
        row_y += row_h + kRowPadding;
    }
    canvas.ClearClipRect();
}

} // namespace SwitchFrontend
