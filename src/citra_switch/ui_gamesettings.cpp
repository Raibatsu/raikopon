// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_gamesettings.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "citra_switch/game_settings.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/settings_model.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 48.0f;
constexpr float kRowPadding = 6.0f;
constexpr float kDescriptionHeight = 32.0f;
constexpr float kDescriptionGap = 8.0f;
constexpr float kRowFocusDuration = 0.15f;
constexpr float kOpenSlideDuration = 0.2f;
constexpr float kOpenSlideDistance = 40.0f;

void CloseGameSettings(UiModel& model) {
    model.game_settings_open = false;
    model.game_settings_was_open = false;
    CommitMenuSettingsPerGame(model.game_settings_before, model.game_settings_current);
    EndGameOverrides();
}

} // namespace

void UpdateGameSettings(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                        GlyphAtlas& atlas, float dt) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    if (!model.game_settings_was_open) {
        model.game_settings_was_open = true;
        model.game_settings_open_t = 0.0f;
    }
    model.game_settings_open_t =
        std::min(1.0f, model.game_settings_open_t + dt / kOpenSlideDuration);
    const float open_offset =
        (1.0f - EaseOutCubic(model.game_settings_open_t)) * kOpenSlideDistance;

    const float content_x = kContentPadding + open_offset;
    const float row_w = screen_w - kContentPadding - content_x;
    float y = kContentPadding;

    const std::string title =
        model.game_settings_title.empty() ? Tr("gamedetails.action.settings") : model.game_settings_title;
    DrawText(canvas, atlas, content_x, y, title, palette.text);
    y += atlas.LineHeight() + kContentPadding * 0.5f;

    const std::vector<SettingRow> rows = BuildPerGameSettingRows(model.game_settings_current);

    if (rows.empty()) {
        DrawText(canvas, atlas, content_x, y, Tr("gamesettings.nothing_here"),
                palette.text_dim);
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chip_count = 1;
        if (input.cancel) {
            CloseGameSettings(model);
        }
        return;
    }
    if (model.game_settings_row >= static_cast<int>(rows.size())) {
        model.game_settings_row = static_cast<int>(rows.size()) - 1;
    }
    if (rows[static_cast<std::size_t>(model.game_settings_row)].is_header) {
        model.game_settings_row = FirstSelectableRow(rows);
    }

    const SettingRow& selected_row = rows[static_cast<std::size_t>(model.game_settings_row)];
    const bool is_boolean = IsBooleanSetting(selected_row.item);

    if (model.game_settings_row_armed) {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.done")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.cancel")};
        model.hint_chip_count = 2;
    } else {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.adjust")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::ResetToDefault), Tr("gamesettings.reset_overrides")};
        model.hint_chip_count = 3;
    }

    if (!model.game_settings_row_armed) {
        if (input.up) {
            model.game_settings_row = NextSelectableRow(rows, model.game_settings_row, -1);
        }
        if (input.down) {
            model.game_settings_row = NextSelectableRow(rows, model.game_settings_row, 1);
        }
        if (input.confirm) {
            if (is_boolean) {
                ToggleSetting(model.game_settings_current, selected_row.item);
                SetMenuSettings(model.game_settings_current);
            } else {
                model.game_settings_row_armed = true;
            }
        }
        if (input.reset_default) {
            ResetGameOverridesToLibrary();
            model.game_settings_current = GetMenuSettings();
            model.game_settings_before = model.game_settings_current;
        }
        if (input.cancel) {
            CloseGameSettings(model);
            return;
        }
    } else {
        if (input.cancel || input.confirm) {
            model.game_settings_row_armed = false;
        } else {
            if (input.left) {
                CycleSetting(model.game_settings_current, selected_row.item, -1);
                SetMenuSettings(model.game_settings_current);
            }
            if (input.right) {
                CycleSetting(model.game_settings_current, selected_row.item, 1);
                SetMenuSettings(model.game_settings_current);
            }
        }
    }

    const float viewport_bottom =
        screen_h - kHintBarHeight - kContentPadding - kDescriptionHeight - kDescriptionGap;
    const float selected_top =
        y + static_cast<float>(model.game_settings_row) * (kRowHeight + kRowPadding);
    const float selected_height = kRowHeight * kSelectedGrowth;
    const float content_height = static_cast<float>(rows.size()) * (kRowHeight + kRowPadding);
    UpdateScroll(model.game_settings_scroll, input, model.game_settings_row, selected_top,
                selected_height, content_x, y, row_w, viewport_bottom - y,
                content_height - (viewport_bottom - y));
    const float scroll_offset = model.game_settings_scroll.offset;

    if (model.game_settings_row_focus_last != model.game_settings_row) {
        model.game_settings_row_focus_last = model.game_settings_row;
        model.game_settings_row_focus_t = 0.0f;
    }
    model.game_settings_row_focus_t =
        std::min(1.0f, model.game_settings_row_focus_t + dt / kRowFocusDuration);

    canvas.SetClipRect(content_x, y, row_w, viewport_bottom - y);
    float row_y = y - scroll_offset;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SettingRow& row = rows[i];

        if (row.is_header) {
            if (row_y + kRowHeight >= y && row_y <= viewport_bottom) {
                DrawText(canvas, atlas, content_x, row_y + (kRowHeight - atlas.LineHeight()) * 0.5f,
                        row.label, palette.accent);
                canvas.DrawQuad(content_x, row_y + kRowHeight - kHeaderUnderlineHeight, row_w,
                                kHeaderUnderlineHeight, palette.accent);
            }
            row_y += kRowHeight + kRowPadding;
            continue;
        }

        const bool selected = static_cast<int>(i) == model.game_settings_row;
        const bool armed = selected && model.game_settings_row_armed;
        const float grow = selected ? 1.0f + (kSelectedGrowth - 1.0f) *
                                                  EaseOutCubic(model.game_settings_row_focus_t)
                                    : 1.0f;
        const float row_h = kRowHeight * grow;

        if (row_y + row_h >= y && row_y <= viewport_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h,
                                armed ? palette.accent_dim : palette.surface_warm);
            }
            const CanvasColor label_color = selected ? palette.text : palette.text_dim;
            const float label_y = row_y + (row_h - atlas.LineHeight()) * 0.5f;
            DrawText(canvas, atlas, content_x + kContentPadding, label_y, row.label, label_color);

            const float value_width = MeasureText(atlas, canvas, row.value);
            const CanvasColor value_color = armed ? palette.accent : palette.text_dim;
            DrawText(canvas, atlas, content_x + row_w - kContentPadding - value_width, label_y,
                    row.value, value_color);
        }
        row_y += row_h + kRowPadding;
    }
    canvas.ClearClipRect();

    const float description_y = viewport_bottom + kDescriptionGap;
    canvas.DrawQuad(content_x, description_y, row_w, kDescriptionHeight, palette.surface);
    canvas.SetClipRect(content_x, description_y, row_w, kDescriptionHeight);
    DrawText(canvas, atlas, content_x, description_y + (kDescriptionHeight - atlas.LineHeight()) * 0.5f,
            selected_row.description, palette.text_dim);
    canvas.ClearClipRect();
}

} // namespace SwitchFrontend
