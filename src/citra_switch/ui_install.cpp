// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_install.h"

#include <algorithm>
#include <cstddef>

#include "citra_switch/config.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 44.0f;
constexpr float kRowPadding = 4.0f;
constexpr float kPanelMarginX = 120.0f;
constexpr float kPanelMarginY = 60.0f;
constexpr float kPanelPadding = 24.0f;

void RefreshInstallListing(UiModel& model) {
    model.install_dirs = ListSubdirectories(model.install_dir);
    model.install_files = ListCiaFiles(model.install_dir);
    model.install_listed = true;
    model.install_selected = 0;
}

bool HasParentDir(const UiModel& model) {
    return !ParentDirectory(model.install_dir).empty();
}

int InstallItemCount(const UiModel& model) {
    return (HasParentDir(model) ? 1 : 0) + static_cast<int>(model.install_dirs.size()) +
           static_cast<int>(model.install_files.size());
}

void DrawScrim(GpuCanvas& canvas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});
}

} // namespace

void UpdateInstall(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    DrawScrim(canvas);

    if (!model.install_listed) {
        if (model.install_dir.empty()) {
            model.install_dir = GetPaths().roms_dir;
        }
        RefreshInstallListing(model);
    }

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.open")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel),
                           HasParentDir(model) ? Tr("hint.up") : Tr("hint.close")};
    model.hint_chip_count = 2;

    const float panel_x = kPanelMarginX;
    const float panel_y = kPanelMarginY;
    const float panel_w = screen_w - kPanelMarginX * 2.0f;
    const float panel_h = screen_h - kPanelMarginY * 2.0f;
    canvas.DrawQuad(panel_x, panel_y, panel_w, panel_h, palette.surface);

    const float content_x = panel_x + kPanelPadding;
    float y = panel_y + kPanelPadding;
    DrawText(canvas, atlas, content_x, y, Tr("install.title"), palette.text);
    y += atlas.LineHeight() + 4.0f;
    DrawText(canvas, atlas, content_x, y, model.install_dir, palette.text_dim);
    y += atlas.LineHeight() + kPanelPadding * 0.5f;

    const float list_top = y;
    const float list_bottom = panel_y + panel_h - kPanelPadding;
    const float row_w = panel_w - kPanelPadding * 2.0f;
    const int item_count = InstallItemCount(model);

    if (item_count == 0) {
        DrawText(canvas, atlas, content_x, list_top, Tr("install.empty"), palette.text_dim);
    }

    if (model.install_selected >= item_count) {
        model.install_selected = item_count > 0 ? item_count - 1 : 0;
    }

    const bool has_parent = HasParentDir(model);
    const int dirs_offset = has_parent ? 1 : 0;
    const int files_offset = dirs_offset + static_cast<int>(model.install_dirs.size());

    if (input.up && model.install_selected > 0) {
        --model.install_selected;
    }
    if (input.down && model.install_selected + 1 < item_count) {
        ++model.install_selected;
    }

    const float selected_top =
        list_top + static_cast<float>(model.install_selected) * (kRowHeight + kRowPadding);
    const float content_height = static_cast<float>(item_count) * (kRowHeight + kRowPadding);
    UpdateScroll(model.install_scroll, input, model.install_selected, selected_top,
                kRowHeight * kSelectedGrowth, content_x, list_top, row_w, list_bottom - list_top,
                content_height - (list_bottom - list_top));
    const float scroll_offset = model.install_scroll.offset;

    canvas.SetClipRect(content_x, list_top, row_w, list_bottom - list_top);

    int tapped_item = -1;
    float row_y = list_top - scroll_offset;
    for (int i = 0; i < item_count; ++i) {
        const bool selected = i == model.install_selected;
        const float row_h = selected ? kRowHeight * kSelectedGrowth : kRowHeight;

        if (row_y + row_h >= list_top && row_y <= list_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h, palette.surface_warm);
            }
            if (input.touch_tap && input.touch_x >= content_x &&
                input.touch_x < content_x + row_w && input.touch_y >= row_y &&
                input.touch_y < row_y + row_h) {
                tapped_item = i;
            }

            std::string label;
            CanvasColor color = selected ? palette.text : palette.text_dim;
            if (has_parent && i == 0) {
                label = "..";
            } else if (i < files_offset) {
                label = "[Folder] " + model.install_dirs[static_cast<std::size_t>(i - dirs_offset)]
                                          .name;
            } else {
                const CiaEntry& entry =
                    model.install_files[static_cast<std::size_t>(i - files_offset)];
                label = entry.name;
                if (!entry.readable) {
                    label += Tr("install.not_readable");
                    color = palette.text_dim;
                }
            }
            DrawText(canvas, atlas, content_x + 8.0f, row_y + (row_h - atlas.LineHeight()) * 0.5f,
                    label, color);
        }

        row_y += row_h + kRowPadding;
    }
    canvas.ClearClipRect();

    int activated = -1;
    if (input.confirm && item_count > 0) {
        activated = model.install_selected;
    }
    if (tapped_item >= 0) {
        model.install_selected = tapped_item;
        activated = tapped_item;
    }

    if (activated >= 0) {
        if (has_parent && activated == 0) {
            model.install_dir = ParentDirectory(model.install_dir);
            model.install_listed = false;
        } else if (activated < files_offset) {
            model.install_dir =
                model.install_dirs[static_cast<std::size_t>(activated - dirs_offset)].path;
            model.install_listed = false;
        } else {
            const CiaEntry& entry =
                model.install_files[static_cast<std::size_t>(activated - files_offset)];
            if (entry.readable) {
                model.pending_install_path = entry.path;
            }
        }
    }

    if (input.cancel) {
        if (has_parent) {
            model.install_dir = ParentDirectory(model.install_dir);
            model.install_listed = false;
        } else {
            model.install_open = false;
            model.install_listed = false;
        }
    }
}

void RunInstall(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas, const std::string& path) {
    const auto render_progress = [&](std::size_t written, std::size_t total) {
        if (!canvas.BeginFrame(CurrentPalette().bg)) {
            return;
        }
        const UiPalette& palette = CurrentPalette();
        const float screen_w = static_cast<float>(canvas.Width());
        const float screen_h = static_cast<float>(canvas.Height());
        DrawScrim(canvas);

        const float bar_w = screen_w * 0.5f;
        const float bar_h = 32.0f;
        const float bar_x = (screen_w - bar_w) * 0.5f;
        const float bar_y = (screen_h - bar_h) * 0.5f;

        const std::string label = Tr("install.installing");
        const float label_width = MeasureText(atlas, canvas, label);
        DrawText(canvas, atlas, (screen_w - label_width) * 0.5f, bar_y - atlas.LineHeight() - 8.0f,
                label, palette.text);

        // 2px accent outline, same colour as the Library row icons' outline (palette.accent) -
        // a slightly larger quad behind the bar's own fill, the same border trick used
        // throughout the rest of the UI (see ui_gamedetails.cpp's DrawCard).
        constexpr float kOutlineWidth = 2.0f;
        canvas.DrawQuad(bar_x - kOutlineWidth, bar_y - kOutlineWidth, bar_w + kOutlineWidth * 2.0f,
                        bar_h + kOutlineWidth * 2.0f, palette.accent);
        canvas.DrawQuad(bar_x, bar_y, bar_w, bar_h, palette.surface);
        if (total > 0) {
            const float fraction =
                std::clamp(static_cast<float>(written) / static_cast<float>(total), 0.0f, 1.0f);
            canvas.DrawQuad(bar_x, bar_y, bar_w * fraction, bar_h, palette.accent);
        }
        canvas.EndFrame();
    };

    render_progress(0, 0);
    const InstallResult result = InstallCia(path, render_progress);

    model.notice_text = InstallResultText(result);
    model.notice_is_error = result != InstallResult::Success;
    if (result == InstallResult::Success) {
        model.games_scanned = false;
    }
    model.install_open = false;
    model.install_listed = false;
}

} // namespace SwitchFrontend
