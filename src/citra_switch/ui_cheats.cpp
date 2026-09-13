// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_cheats.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "citra_switch/library_cheats.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 48.0f;
constexpr float kRowPadding = 6.0f;

} // namespace

void UpdateCheatsScreen(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                        GlyphAtlas& atlas, float dt) {
    (void)dt;
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const float content_x = kContentPadding;
    const float row_w = screen_w - kContentPadding * 2.0f;
    float y = kContentPadding;

    const std::string title =
        model.cheats_title.empty() ? Tr("gamedetails.action.cheats") : model.cheats_title;
    DrawText(canvas, atlas, content_x, y, title, palette.text);
    y += atlas.LineHeight() + kContentPadding * 0.5f;

    const int row_count = 1 + LibraryCheatCount();
    if (model.cheats_row >= row_count) {
        model.cheats_row = row_count - 1;
    }
    const bool on_cheat_row = model.cheats_row > 0;

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm),
                           on_cheat_row ? Tr("cheats.toggle") : Tr("cheats.add")};
    if (on_cheat_row) {
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Minus), Tr("cheats.edit")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Plus), Tr("cheats.delete")};
        model.hint_chips[3] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chip_count = 4;
    } else {
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chip_count = 2;
    }

    if (input.up && model.cheats_row > 0) {
        --model.cheats_row;
    }
    if (input.down && model.cheats_row + 1 < row_count) {
        ++model.cheats_row;
    }

    if (input.cancel) {
        PersistLibraryCheats();
        model.cheats_open = false;
        return;
    }
    if (input.confirm) {
        if (model.cheats_row == 0) {
            model.cheats_pending_edit_index = -1;
        } else {
            ToggleLibraryCheat(model.cheats_row - 1);
        }
    } else if (on_cheat_row && input.minus) {
        model.cheats_pending_edit_index = model.cheats_row - 1;
    } else if (on_cheat_row && input.plus) {
        const int deleted_index = model.cheats_row - 1;
        DeleteLibraryCheatFlow(deleted_index);
        const int new_count = LibraryCheatCount();
        model.cheats_row = new_count > 0 ? 1 + std::min(deleted_index, new_count - 1) : 0;
    }

    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;
    const int final_row_count = 1 + LibraryCheatCount();
    if (model.cheats_row >= final_row_count) {
        model.cheats_row = final_row_count - 1;
    }

    canvas.SetClipRect(content_x, y, row_w, viewport_bottom - y);
    float row_y = y;
    for (int i = 0; i < final_row_count; ++i) {
        const bool selected = i == model.cheats_row;
        const float row_h = selected ? kRowHeight * kSelectedGrowth : kRowHeight;
        if (row_y + row_h >= y && row_y <= viewport_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h, palette.surface_warm);
            }
            const float label_y = row_y + (row_h - atlas.LineHeight()) * 0.5f;
            const std::string label = i == 0 ? Tr("cheats.add") : LibraryCheatName(i - 1);
            DrawText(canvas, atlas, content_x + kContentPadding, label_y, label,
                    selected ? palette.text : palette.text_dim);
            if (i > 0) {
                const std::string value =
                    LibraryCheatEnabled(i - 1) ? Tr("common.on") : Tr("common.off");
                const float value_width = MeasureText(atlas, canvas, value);
                DrawText(canvas, atlas, content_x + row_w - kContentPadding - value_width, label_y,
                        value, selected ? palette.accent : palette.text_dim);
            }
        }
        row_y += row_h + kRowPadding;
    }
    canvas.ClearClipRect();
}

} // namespace SwitchFrontend
