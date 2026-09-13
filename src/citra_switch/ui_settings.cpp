// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_settings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "citra_switch/config.h"
#include "citra_switch/gametdb.h"
#include "citra_switch/settings_model.h"
#include "citra_switch/ui_controls.h"
#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_navbar.h"
#include "citra_switch/ui_page_transition.h"
#include "citra_switch/ui_paths.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"
#include "citra_switch/ui_updates.h"

namespace SwitchFrontend {

namespace {

constexpr float kTabStripHeight = kRailItemHeight;
constexpr float kRowHeight = 48.0f;
constexpr float kRowPadding = 6.0f;
constexpr float kPercentBarWidth = 160.0f;
constexpr float kPercentBarHeight = 28.0f;
constexpr float kGyroBarWidth = 110.0f;

constexpr float kDescriptionHeight = 32.0f;
constexpr float kDescriptionGap = 8.0f;
constexpr float kDescriptionScrollSpeed = 40.0f;
constexpr float kDescriptionPauseSeconds = 1.0f;

constexpr int kWaveStrips = 7;
constexpr float kWaveAmplitude = 3.0f;
constexpr float kWaveCyclesOverHeight = 1.5f;
constexpr float kWaveSpeed = 2.4f;

constexpr float kRowFocusDuration = 0.15f;
constexpr float kTabRevealDuration = 0.22f;
constexpr float kTabRevealDistance = 60.0f;

constexpr float kConfirmPanelMinWidth = 360.0f;
constexpr float kConfirmPanelMaxMargin = 80.0f;
constexpr float kConfirmPanelPaddingX = 40.0f;
constexpr float kConfirmPanelHeight = 180.0f;
constexpr float kConfirmButtonWidth = 160.0f;
constexpr float kConfirmButtonHeight = 48.0f;
constexpr float kConfirmButtonGap = 24.0f;

void DrawBoldText(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float y, const std::string& text,
                  CanvasColor color) {
    DrawText(canvas, atlas, x, y, text, color);
    DrawText(canvas, atlas, x + 1.0f, y, text, color);
}

void DrawPercentBar(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float y, float w, float h,
                    float fraction, const std::string& label, const UiPalette& palette,
                    bool highlighted, float wave_phase) {
    canvas.DrawQuad(x, y, w, h, palette.surface);
    const float fill_w = w * std::clamp(fraction, 0.0f, 1.0f);
    if (fill_w > 0.0f) {
        const CanvasColor fill_color = highlighted ? palette.accent : palette.accent_dim;
        const float strip_h = h / static_cast<float>(kWaveStrips);
        for (int i = 0; i < kWaveStrips; ++i) {
            const float strip_center = strip_h * (static_cast<float>(i) + 0.5f);
            const float wave = std::sin(wave_phase + strip_center *
                                        (kWaveCyclesOverHeight * 6.2831853f / h)) *
                               kWaveAmplitude;
            const float strip_fill_w = std::clamp(fill_w + wave, 0.0f, w);
            if (strip_fill_w > 0.0f) {
                canvas.DrawQuad(x, y + strip_h * static_cast<float>(i), strip_fill_w, strip_h,
                                fill_color);
            }
        }
    }
    const float text_width = MeasureText(atlas, canvas, label);
    DrawText(canvas, atlas, x + (w - text_width) * 0.5f, y + (h - atlas.LineHeight()) * 0.5f,
            label, palette.text);
}

// Shows the selected row's description below the list, auto-scrolling it left (with a brief
// pause before the scroll starts) if it's wider than the panel. Restarts from the beginning
// whenever the selected row changes - tracked via model.settings_hint_row.
void DrawDescriptionPanel(GpuCanvas& canvas, GlyphAtlas& atlas, UiModel& model, float dt, float x,
                          float y, float w, float h, const std::string& description) {
    const UiPalette& palette = CurrentPalette();
    canvas.DrawQuad(x, y, w, h, palette.surface);

    if (model.settings_hint_row != model.settings_row) {
        model.settings_hint_row = model.settings_row;
        model.settings_hint_scroll = 0.0f;
    }

    const float text_width = MeasureText(atlas, canvas, description);
    const float text_y = y + (h - atlas.LineHeight()) * 0.5f;
    canvas.SetClipRect(x, y, w, h);
    if (text_width <= w) {
        DrawText(canvas, atlas, x, text_y, description, palette.text_dim);
        model.settings_hint_scroll = 0.0f;
    } else {
        const float pause_px = kDescriptionScrollSpeed * kDescriptionPauseSeconds;
        const float cycle = pause_px + text_width + w;
        model.settings_hint_scroll = std::fmod(model.settings_hint_scroll + dt * kDescriptionScrollSpeed, cycle);
        const float draw_offset = std::max(0.0f, model.settings_hint_scroll - pause_px);
        DrawText(canvas, atlas, x - draw_offset, text_y, description, palette.text_dim);
    }
    canvas.ClearClipRect();
}

bool IsGyroRow(SettingRowIdx item) {
    return item == SettingRowGyroSensitivity;
}

bool IsPercentRow(SettingRowIdx item) {
    return item == SettingRowCpuClock || item == SettingRowGyroSensitivity ||
           item == SettingRowMovieThrottle;
}

bool IsCyclableRow(SettingRowIdx item) {
    switch (item) {
    case SettingRowResolution:
    case SettingRowTextureFilter:
    case SettingRowCpuClock:
    case SettingRowRegion:
    case SettingRowLanguage:
    case SettingRowPointerSource:
    case SettingRowMovieThrottle:
        return true;
    default:
        return false;
    }
}

// A true modal - drawn instead of (not over) the settings content beneath it, matching
// ui_install.cpp's DrawScrim convention rather than trying to show the frozen content through a
// translucent overlay. Left/Right move the Yes/No selection; Confirm activates it; Cancel always
// means No, the same safe default a stray press lands on.
void DrawSettingsConfirmPopup(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                              GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});

    const float message_width = MeasureText(atlas, canvas, model.settings_confirm_message);
    const float buttons_total_w = kConfirmButtonWidth * 2.0f + kConfirmButtonGap;
    // Sized to fit the message (which varies a lot - "Reset all settings?" vs. a specific row's
    // label + explanation), clamped between a sane minimum and the screen width so a pathologically
    // long message can't run off the edges.
    const float panel_w =
        std::clamp(std::max(message_width, buttons_total_w) + kConfirmPanelPaddingX * 2.0f,
                  kConfirmPanelMinWidth, screen_w - kConfirmPanelMaxMargin * 2.0f);

    const float panel_x = (screen_w - panel_w) * 0.5f;
    const float panel_y = (screen_h - kConfirmPanelHeight) * 0.5f;
    canvas.DrawQuad(panel_x, panel_y, panel_w, kConfirmPanelHeight, palette.surface);

    DrawText(canvas, atlas, panel_x + (panel_w - message_width) * 0.5f, panel_y + 40.0f,
            model.settings_confirm_message, palette.text);

    if (input.left) {
        model.settings_confirm_selected = 0;
    }
    if (input.right) {
        model.settings_confirm_selected = 1;
    }

    const std::string labels[2] = {Tr("confirm.yes"), Tr("confirm.no")};
    const float buttons_x = panel_x + (panel_w - buttons_total_w) * 0.5f;
    const float buttons_y = panel_y + kConfirmPanelHeight - kConfirmButtonHeight - 28.0f;

    int tapped = -1;
    for (int i = 0; i < 2; ++i) {
        const float bx =
            buttons_x + static_cast<float>(i) * (kConfirmButtonWidth + kConfirmButtonGap);
        const bool selected = model.settings_confirm_selected == i;
        canvas.DrawQuad(bx, buttons_y, kConfirmButtonWidth, kConfirmButtonHeight,
                        selected ? palette.accent_dim : palette.surface_warm);
        const float label_w = MeasureText(atlas, canvas, labels[i]);
        DrawText(canvas, atlas, bx + (kConfirmButtonWidth - label_w) * 0.5f,
                buttons_y + (kConfirmButtonHeight - atlas.LineHeight()) * 0.5f, labels[i],
                selected ? palette.text : palette.text_dim);
        if (input.touch_tap && input.touch_x >= bx && input.touch_x < bx + kConfirmButtonWidth &&
            input.touch_y >= buttons_y && input.touch_y < buttons_y + kConfirmButtonHeight) {
            tapped = i;
        }
    }

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.select")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("confirm.no")};
    model.hint_chip_count = 2;

    bool decided = false;
    bool yes = false;
    if (tapped >= 0) {
        model.settings_confirm_selected = tapped;
        decided = true;
        yes = tapped == 0;
    } else if (input.confirm) {
        decided = true;
        yes = model.settings_confirm_selected == 0;
    } else if (input.cancel) {
        decided = true;
        yes = false;
    }

    if (!decided) {
        return;
    }

    const SettingsConfirmKind kind = model.settings_confirm_kind;
    model.settings_confirm_kind = SettingsConfirmKind::None;
    model.settings_confirm_selected = 1;

    if (kind == SettingsConfirmKind::ResetToDefault) {
        // "No" never had anything pre-applied to undo - the reset itself only happens here.
        if (yes) {
            model.settings = model.settings_defaults;
            SetMenuSettings(model.settings);
            atlas.SetLanguage(model.settings.language);
            SetUiLanguage(model.settings.language);
        }
    } else if (kind == SettingsConfirmKind::RestartRequired) {
        // The new value is already live (applied at toggle/arm time) - "Yes" just exits so the
        // user can relaunch and pick it up; "No" reverts to the pre-change snapshot.
        if (yes) {
            model.exit_requested = true;
        } else {
            model.settings = model.settings_confirm_snapshot;
            SetMenuSettings(model.settings);
            atlas.SetLanguage(model.settings.language);
            SetUiLanguage(model.settings.language);
        }
    } else if (kind == SettingsConfirmKind::ResetControls) {
        if (yes) {
            ResetAllControlsToDefault();
        }
    }
}

constexpr float kCoverPopupWidth = 420.0f;
constexpr float kCoverPopupHeight = 160.0f;
constexpr float kCoverBarWidth = 320.0f;
constexpr float kCoverBarHeight = 32.0f;

// Same scrim-and-panel convention as DrawSettingsConfirmPopup, but non-blocking: the actual
// download runs on gametdb.cpp's own background thread, this just polls its progress every
// frame and draws it with the same animated wave-fill bar the gyro/percent rows use
// (DrawPercentBar). Deliberately has no Cancel - matches ui_install.cpp's CIA install popup,
// which can't be interrupted either - and closes itself the instant the download finishes
// (success or a lost connection) rather than waiting for the user to dismiss it; either outcome
// gets a one-line notice banner on the settings screen underneath instead.
void DrawCoverDownloadPopup(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                            GlyphAtlas& atlas) {
    (void)input;
    const CoverDownloadProgress progress = GetCoverDownloadProgress();
    if (!IsCoverDownloadRunning()) {
        model.cover_download_open = false;
        if (progress.connection_error) {
            model.notice_text = Tr("covers.connection_error");
            model.notice_is_error = true;
        } else {
            model.notice_text =
                std::to_string(progress.covers_found) + Tr("covers.found_suffix");
            model.notice_is_error = false;
        }
        return;
    }

    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});

    const float panel_x = (screen_w - kCoverPopupWidth) * 0.5f;
    const float panel_y = (screen_h - kCoverPopupHeight) * 0.5f;
    canvas.DrawQuad(panel_x, panel_y, kCoverPopupWidth, kCoverPopupHeight, palette.surface);

    const std::string title = Tr("covers.downloading");
    const float title_width = MeasureText(atlas, canvas, title);
    DrawText(canvas, atlas, panel_x + (kCoverPopupWidth - title_width) * 0.5f, panel_y + 28.0f,
            title, palette.text);

    const float bar_x = panel_x + (kCoverPopupWidth - kCoverBarWidth) * 0.5f;
    const float bar_y = panel_y + 66.0f;
    const float fraction =
        progress.games_total > 0
            ? static_cast<float>(progress.games_done) / static_cast<float>(progress.games_total)
            : 0.0f;
    const std::string bar_label = std::to_string(progress.games_done) + " / " +
                                  std::to_string(progress.games_total) + Tr("covers.games_suffix");
    DrawPercentBar(canvas, atlas, bar_x, bar_y, kCoverBarWidth, kCoverBarHeight, fraction,
                   bar_label, palette, true, model.wave_elapsed * kWaveSpeed);

    const std::string found_label =
        std::to_string(progress.covers_found) + Tr("covers.found_suffix");
    const float found_width = MeasureText(atlas, canvas, found_label);
    DrawText(canvas, atlas, panel_x + (kCoverPopupWidth - found_width) * 0.5f,
            bar_y + kCoverBarHeight + 14.0f, found_label, palette.text_dim);

    model.hint_chip_count = 0;
}

// Full-screen multi-select list of the screen layouts the screen-swap key cycles through - same
// row shape as ui_mods.cpp's UpdateModsScreen (growth-on-select, right-aligned On/Off in accent
// color), not a floating popup, so it matches the rest of the app instead of standing out. Toggles
// model.settings.layout_cycle_mask bit `layout_cycle_picker_row` on Confirm, closes on
// Cancel/Plus. Always leaves at least one layout enabled, otherwise the cycle key would have
// nothing to land on.
void DrawLayoutCyclePicker(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                           GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const float content_x = kContentPadding;
    const float row_w = screen_w - kContentPadding * 2.0f;
    float y = kContentPadding;

    DrawText(canvas, atlas, content_x, y, Tr("settings.layout_cycle.title"), palette.text);
    y += atlas.LineHeight() + kContentPadding * 0.5f;

    const int row_count = GetScreenLayoutCount();
    if (model.layout_cycle_picker_row >= row_count) {
        model.layout_cycle_picker_row = row_count - 1;
    }
    if (model.layout_cycle_picker_row < 0) {
        model.layout_cycle_picker_row = 0;
    }

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.toggle")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.close")};
    model.hint_chip_count = 2;

    if (input.cancel || input.plus) {
        model.layout_cycle_picker_open = false;
        return;
    }
    if (input.up && model.layout_cycle_picker_row > 0) {
        --model.layout_cycle_picker_row;
    }
    if (input.down && model.layout_cycle_picker_row + 1 < row_count) {
        ++model.layout_cycle_picker_row;
    }
    if (input.confirm) {
        const std::uint32_t bit = 1u << model.layout_cycle_picker_row;
        const std::uint32_t new_mask = model.settings.layout_cycle_mask ^ bit;
        if (new_mask != 0) {
            model.settings.layout_cycle_mask = new_mask;
            SetMenuSettings(model.settings);
        }
    }

    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;
    canvas.SetClipRect(content_x, y, row_w, viewport_bottom - y);
    float row_y = y;
    for (int i = 0; i < row_count; ++i) {
        const bool selected = i == model.layout_cycle_picker_row;
        const float row_h = selected ? kRowHeight * kSelectedGrowth : kRowHeight;
        if (row_y + row_h >= y && row_y <= viewport_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h, palette.surface_warm);
            }
            const float label_y = row_y + (row_h - atlas.LineHeight()) * 0.5f;
            DrawText(canvas, atlas, content_x + kContentPadding, label_y, GetScreenLayoutName(i),
                    selected ? palette.text : palette.text_dim);
            const bool enabled = (model.settings.layout_cycle_mask & (1u << i)) != 0;
            const std::string value = enabled ? Tr("common.on") : Tr("common.off");
            const float value_width = MeasureText(atlas, canvas, value);
            DrawText(canvas, atlas, content_x + row_w - kContentPadding - value_width, label_y,
                    value, selected ? palette.accent : palette.text_dim);
        }
        row_y += row_h + kRowPadding;
    }
    canvas.ClearClipRect();
}

} // namespace

void UpdateSettings(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                    float dt) {
    if (!model.settings_loaded) {
        model.settings = GetMenuSettings();
        model.settings_defaults = DefaultMenuSettings();
        model.settings_loaded = true;
    }

    if (model.settings_confirm_kind != SettingsConfirmKind::None) {
        DrawSettingsConfirmPopup(model, input, canvas, atlas);
        return;
    }
    if (model.cover_download_open) {
        DrawCoverDownloadPopup(model, input, canvas, atlas);
        return;
    }
    if (model.layout_cycle_picker_open) {
        DrawLayoutCyclePicker(model, input, canvas, atlas);
        return;
    }
    if (model.changelog_popup_open) {
        DrawChangelogPopup(model, input, canvas, atlas);
        return;
    }

    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    // The rail is reachable directly via ZL/ZR (RailPrev/RailNext), or by backing all the way out
    // (Cancel) from the tab strip/row list - but not via Left, which cyclable rows need for
    // adjusting Internal Resolution, Texture Filter, etc. Once focused, Up/Down and ZL/ZR both
    // cycle the (now 3) rail items; Confirm/Right hand focus back to Settings' own content
    // (Confirm on Exit closes the app instead - Right never does, as a small deliberate-action
    // safeguard).
    const bool was_rail_focused = model.rail_focused;
    const bool rail_entry_allowed =
        !model.settings_row_armed && !model.controls_capturing && !model.paths_browse_open;
    if (!model.rail_focused && rail_entry_allowed &&
        (input.cancel || input.rail_prev || input.rail_next)) {
        model.rail_focused = true;
    }
    if (model.rail_focused) {
        int rail_index = static_cast<int>(model.active_rail);
        if (input.up || input.rail_prev) {
            rail_index = (rail_index + kNumRailItems - 1) % kNumRailItems;
        }
        if (input.down || input.rail_next) {
            rail_index = (rail_index + 1) % kNumRailItems;
        }
        const RailItem new_rail = static_cast<RailItem>(rail_index);
        if (new_rail != model.active_rail) {
            if (new_rail == RailItem::Library) {
                TriggerPageTransition(model, canvas, atlas, new_rail, /*commit=*/false);
                return;
            }
            model.active_rail = new_rail;
        }

        if (input.confirm) {
            if (model.active_rail == RailItem::Exit) {
                model.exit_requested = true;
                return;
            }
            model.rail_focused = false;
        } else if (input.right && model.active_rail != RailItem::Exit) {
            model.rail_focused = false;
        } else if (input.cancel && model.active_rail == RailItem::Exit) {
            TriggerPageTransition(model, canvas, atlas, RailItem::Library, /*commit=*/false);
            return;
        }
    }

    const NavItem rail_items[] = {{Tr("nav.library"), GetIconRegion(Icon::Library)},
                                  {Tr("nav.settings"), GetIconRegion(Icon::Settings)},
                                  {Tr("nav.credits"), GetIconRegion(Icon::Credits)},
                                  {Tr("nav.exit"), GetIconRegion(Icon::Exit)}};

    const float rail_target =
        model.rail_focused
            ? NavBarActiveTabWidth(canvas, atlas,
                                   rail_items[static_cast<int>(model.active_rail)].label)
            : 0.0f;
    model.rail_pop_anim =
        rail_target + (model.rail_pop_anim - rail_target) * std::exp(-dt * 10.0f);

    const int tapped_rail =
        DrawNavBar(canvas, atlas, rail_items, kNumRailItems, static_cast<int>(model.active_rail),
                  0.0f, screen_h * 0.5f, kRailCollapsedWidth, kRailItemHeight, true, input,
                  model.wave_elapsed);
    if (tapped_rail == static_cast<int>(RailItem::Exit)) {
        model.exit_requested = true;
        return;
    } else if (tapped_rail == static_cast<int>(RailItem::Library)) {
        TriggerPageTransition(model, canvas, atlas, RailItem::Library);
        return;
    } else if (tapped_rail == static_cast<int>(RailItem::Settings)) {
        model.rail_focused = false;
    } else if (tapped_rail == static_cast<int>(RailItem::Credits)) {
        model.active_rail = RailItem::Credits;
        model.rail_focused = false;
    }

    if (model.active_rail != RailItem::Settings) {
        return;
    }

    // Draws the active rail item's bookmark tab (if popped) on top of whatever content this
    // frame draws below, no matter which of this function's several return paths gets hit.
    const NavBarActiveTabGuard active_tab_guard{canvas,
                                                atlas,
                                                rail_items,
                                                kNumRailItems,
                                                static_cast<int>(model.active_rail),
                                                0.0f,
                                                screen_h * 0.5f,
                                                kRailCollapsedWidth,
                                                kRailItemHeight,
                                                model.rail_pop_anim};

    const float content_x = kRailContentX;

    // Unlike the early-return this replaced, Settings' tab strip and rows now keep drawing
    // (frozen) while the rail has focus, matching Library. Input is frozen instead by routing
    // everything below through a neutral, all-false MenuInput - `was_rail_focused` covers the
    // same-frame exit-transition too, so the Right/Confirm press that handed focus back doesn't
    // also cascade into arming a row or switching tabs.
    const MenuInput kNoInput{};
    const MenuInput& content_input = (model.rail_focused || was_rail_focused) ? kNoInput : input;

    // While the rail has focus, its own hint chips take priority over whatever the frozen content
    // underneath would otherwise show - applied at every return point below via this lambda.
    auto ApplyRailFocusHints = [&]() {
        if (model.rail_focused) {
            model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::RailPrev), Tr("hint.prev")};
            model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::RailNext), Tr("hint.next")};
            model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.select")};
            model.hint_chip_count = 3;
        }
    };

    const float tab_count = static_cast<float>(kNumSettingsTabs);
    const float tab_item_w = (screen_w - content_x) / tab_count;

    const auto tab_tr_key = [](SettingsTab tab) {
        switch (tab) {
        case SettingsTab::Display:
            return "tab.display";
        case SettingsTab::Performance:
            return "tab.performance";
        case SettingsTab::Advanced:
            return "tab.advanced";
        case SettingsTab::System:
            return "tab.system";
        case SettingsTab::Paths:
            return "tab.paths";
        case SettingsTab::Controls:
            return "tab.controls";
        case SettingsTab::Updates:
            return "tab.updates";
        }
        return "";
    };
    NavItem tab_items[kNumSettingsTabs];
    for (int i = 0; i < kNumSettingsTabs; ++i) {
        tab_items[i] = NavItem{Tr(tab_tr_key(kSettingsTabs[static_cast<std::size_t>(i)].first))};
    }
    const int tapped_tab = DrawNavBar(canvas, atlas, tab_items, kNumSettingsTabs,
                                      model.settings_tab_index, content_x, 0.0f, kTabStripHeight,
                                      tab_item_w, false, content_input, model.wave_elapsed);
    const auto switch_tab = [&](int new_index, int dir) {
        model.settings_tab_index = new_index;
        const SettingsTab new_tab = kSettingsTabs[static_cast<std::size_t>(new_index)].first;
        model.settings_row = FirstSelectableRow(BuildSettingRows(new_tab, model.settings));
        model.settings_row_armed = false;
        model.settings_tab_reveal_t = 0.0f;
        model.settings_tab_reveal_dir = dir;
    };
    if (tapped_tab >= 0 && tapped_tab != model.settings_tab_index) {
        switch_tab(tapped_tab, tapped_tab > model.settings_tab_index ? 1 : -1);
    }

    if ((content_input.tab_prev || content_input.tab_next) && !model.settings_row_armed &&
        !model.controls_capturing) {
        const int delta = content_input.tab_next ? 1 : -1;
        const int clamped = std::clamp(model.settings_tab_index + delta, 0, kNumSettingsTabs - 1);
        if (clamped != model.settings_tab_index) {
            switch_tab(clamped, delta);
        }
    }

    const SettingsTab current_tab =
        kSettingsTabs[static_cast<std::size_t>(model.settings_tab_index)].first;

    const float content_top = kContentPadding + kTabStripHeight;
    const float row_w = screen_w - content_x - kContentPadding;
    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;

    if (current_tab == SettingsTab::Paths) {
        UpdatePathsTab(model, content_input, canvas, atlas, dt, content_x, content_top, row_w,
                       viewport_bottom);
        ApplyRailFocusHints();
        return;
    }
    if (current_tab == SettingsTab::Controls) {
        UpdateControlsTab(model, content_input, canvas, atlas, dt, content_x, content_top, row_w,
                          viewport_bottom);
        ApplyRailFocusHints();
        return;
    }
    if (current_tab == SettingsTab::Updates) {
        UpdateUpdatesTab(model, content_input, canvas, atlas, dt, content_x, content_top, row_w,
                         viewport_bottom);
        ApplyRailFocusHints();
        return;
    }
    const std::vector<SettingRow> rows = BuildSettingRows(current_tab, model.settings);
    const std::vector<SettingRow> default_rows =
        BuildSettingRows(current_tab, model.settings_defaults);

    if (rows.empty()) {
        DrawText(canvas, atlas, content_x, content_top, Tr("settings.nothing_here"),
                palette.text_dim);
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::TabPrev), Tr("hint.prev_tab")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::TabNext), Tr("hint.next_tab")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chip_count = 3;
        ApplyRailFocusHints();
        return;
    }
    if (model.settings_row >= static_cast<int>(rows.size())) {
        model.settings_row = static_cast<int>(rows.size()) - 1;
    }
    if (rows[static_cast<std::size_t>(model.settings_row)].is_header) {
        model.settings_row = FirstSelectableRow(rows);
    }

    const SettingRow& selected_row = rows[static_cast<std::size_t>(model.settings_row)];
    const bool is_gyro = IsGyroRow(selected_row.item);
    const bool is_cyclable = IsCyclableRow(selected_row.item);

    // Opens the RestartRequired popup for a row whose new value has already taken effect live -
    // it only ever offers to exit (Yes) or revert to whatever was snapshotted just before the
    // change (No), never a "restart in place" option, since the frontend can't restart itself.
    const auto begin_restart_confirm = [&](const SettingRow& row) {
        model.settings_confirm_kind = SettingsConfirmKind::RestartRequired;
        model.settings_confirm_message =
            std::string(row.label) + Tr("settings.restart_required_suffix");
        model.settings_confirm_selected = 1;
    };
    // Boolean rows toggle (and apply) immediately, matching the existing instant-toggle UX;
    // cyclable/gyro rows just arm for joystick adjustment. Either way, a restart-required row
    // snapshots the settings right before the live change so a later "No" has something to
    // revert to.
    const auto activate_row = [&](const SettingRow& row) {
        if (IsBooleanSetting(row.item)) {
            const bool requires_restart = RequiresRestart(row.item);
            if (requires_restart) {
                model.settings_confirm_snapshot = model.settings;
            }
            ToggleSetting(model.settings, row.item);
            SetMenuSettings(model.settings);
            if (requires_restart) {
                begin_restart_confirm(row);
            }
        } else if (IsGyroRow(row.item) || IsCyclableRow(row.item)) {
            model.settings_row_armed = true;
            model.settings_gyro_axis = 0;
            if (RequiresRestart(row.item)) {
                model.settings_confirm_snapshot = model.settings;
            }
        } else if (row.item == SettingRowDownloadCovers) {
            // Doesn't arm for adjustment like a cyclable row - Confirm here just (re)opens the
            // progress popup, starting a new download only if one isn't already running.
            if (!IsCoverDownloadRunning()) {
                StartCoverDownload(model.games);
            }
            model.cover_download_open = true;
        } else if (row.item == SettingRowLayoutCycle) {
            model.layout_cycle_picker_open = true;
            model.layout_cycle_picker_row = 0;
        }
    };
    // Un-arms a cyclable/gyro row; for a restart-required one, checks whether its value actually
    // changed since it was armed (reusing the same off-default string-comparison pattern the row
    // list already uses for bold text) and only then opens the popup - cycling back to the
    // original value and un-arming shouldn't prompt for nothing. Also where the armed adjustment's
    // value actually gets persisted to disk - each held-repeat tick while armed only applies it
    // live (ApplyMenuSettings, no disk write), so holding a direction to rapidly cycle through
    // values doesn't write the whole config file to disk on every single step.
    const auto deactivate_row = [&](const SettingRow& row) {
        model.settings_row_armed = false;
        if (row.item == SettingRowResolution) {
            ApplyMenuSettings(model.settings);
        }
        SaveConfig();
        if (IsCyclableRow(row.item) && RequiresRestart(row.item)) {
            const std::vector<SettingRow> snapshot_rows =
                BuildSettingRows(current_tab, model.settings_confirm_snapshot);
            if (row.value != snapshot_rows[static_cast<std::size_t>(model.settings_row)].value) {
                begin_restart_confirm(row);
            }
        }
    };

    if (model.settings_row_armed) {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.done")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.cancel")};
        model.hint_chip_count = 2;
    } else {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.adjust")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::TabPrev), Tr("hint.prev_tab")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::TabNext), Tr("hint.next_tab")};
        model.hint_chips[3] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
        model.hint_chips[4] = {PrimaryButtonLabel(MenuAction::ResetToDefault), Tr("hint.reset")};
        model.hint_chip_count = 5;
    }

    if (!model.settings_row_armed) {
        if (content_input.up) {
            model.settings_row = NextSelectableRow(rows, model.settings_row, -1);
        }
        if (content_input.down) {
            model.settings_row = NextSelectableRow(rows, model.settings_row, 1);
        }
        if (content_input.confirm) {
            activate_row(selected_row);
        }
        // Resets every setting app-wide, not just this tab's rows - gated behind a confirmation
        // popup rather than applied immediately, since it can't be undone the way a single row's
        // change can.
        if (content_input.reset_default) {
            model.settings_confirm_kind = SettingsConfirmKind::ResetToDefault;
            model.settings_confirm_message = Tr("confirm.reset_all");
            model.settings_confirm_selected = 1;
        }
    } else {
        if (content_input.cancel || content_input.confirm) {
            deactivate_row(selected_row);
        } else if (is_gyro) {
            // Direct assignment, not a toggle - up/down auto-repeat while held (like list
            // navigation does), so a toggle-on-every-fire would flip back and forth and could
            // easily land back on X by the time the button's released.
            if (content_input.up) {
                model.settings_gyro_axis = 0;
            }
            if (content_input.down) {
                model.settings_gyro_axis = 1;
            }
            if (content_input.left) {
                AdjustGyroAxis(model.settings, model.settings_gyro_axis == 1, -1);
                ApplyMenuSettings(model.settings);
            }
            if (content_input.right) {
                AdjustGyroAxis(model.settings, model.settings_gyro_axis == 1, 1);
                ApplyMenuSettings(model.settings);
            }
        } else if (is_cyclable) {
            if (content_input.left) {
                CycleSetting(model.settings, selected_row.item, -1);
                if (selected_row.item != SettingRowResolution) {
                    ApplyMenuSettings(model.settings);
                }
            }
            if (content_input.right) {
                CycleSetting(model.settings, selected_row.item, 1);
                if (selected_row.item != SettingRowResolution) {
                    ApplyMenuSettings(model.settings);
                }
            }
            // The UI's own language follows System Language directly - loads whatever shared
            // font(s) the new language needs (a no-op if they're already loaded from a previous
            // switch, see glyph_atlas.cpp's SetLanguage) and switches Tr()'s lookups over to it.
            if (selected_row.item == SettingRowLanguage && (content_input.left || content_input.right)) {
                atlas.SetLanguage(model.settings.language);
                SetUiLanguage(model.settings.language);
            }
        }
    }

    if (model.settings_row_focus_last != model.settings_row) {
        model.settings_row_focus_last = model.settings_row;
        model.settings_row_focus_t = 0.0f;
    }
    model.settings_row_focus_t = std::min(1.0f, model.settings_row_focus_t + dt / kRowFocusDuration);
    model.settings_tab_reveal_t = std::min(1.0f, model.settings_tab_reveal_t + dt / kTabRevealDuration);
    const float reveal_offset =
        (1.0f - EaseOutCubic(model.settings_tab_reveal_t)) * kTabRevealDistance *
        static_cast<float>(-model.settings_tab_reveal_dir);

    const float row_viewport_bottom = viewport_bottom - kDescriptionHeight - kDescriptionGap;

    const float selected_top =
        content_top + static_cast<float>(model.settings_row) * (kRowHeight + kRowPadding);
    const float selected_height = kRowHeight * kSelectedGrowth;
    const float content_height = static_cast<float>(rows.size()) * (kRowHeight + kRowPadding);
    UpdateScroll(model.settings_scroll, content_input, model.settings_row, selected_top,
                selected_height, content_x, content_top, row_w, row_viewport_bottom - content_top,
                content_height - (row_viewport_bottom - content_top));
    const float scroll_offset = model.settings_scroll.offset;

    canvas.SetClipRect(content_x, content_top, row_w, row_viewport_bottom - content_top);

    int tapped_row = -1;
    float y = content_top - scroll_offset;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const SettingRow& row = rows[i];
        const float row_x = content_x + reveal_offset;

        if (row.is_header) {
            if (y + kRowHeight >= content_top && y <= row_viewport_bottom) {
                DrawText(canvas, atlas, row_x, y + (kRowHeight - atlas.LineHeight()) * 0.5f,
                        row.label, palette.accent);
                canvas.DrawQuad(row_x, y + kRowHeight - kHeaderUnderlineHeight, row_w,
                                kHeaderUnderlineHeight, palette.accent);
            }
            y += kRowHeight + kRowPadding;
            continue;
        }

        const bool selected = static_cast<int>(i) == model.settings_row;
        const bool armed = selected && model.settings_row_armed;
        const float grow = selected ? 1.0f + (kSelectedGrowth - 1.0f) *
                                                  EaseOutCubic(model.settings_row_focus_t)
                                    : 1.0f;
        const float row_h = kRowHeight * grow;

        if (y + row_h >= content_top && y <= row_viewport_bottom) {
            if (selected) {
                canvas.DrawQuad(row_x, y, row_w, row_h, armed ? palette.accent_dim
                                                              : palette.surface_warm);
            }

            if (content_input.touch_tap && content_input.touch_x >= row_x &&
                content_input.touch_x < row_x + row_w && content_input.touch_y >= y &&
                content_input.touch_y < y + row_h) {
                tapped_row = static_cast<int>(i);
            }

            const bool off_default = row.value != default_rows[i].value;
            const CanvasColor label_color = selected ? palette.text : palette.text_dim;
            const float label_y = y + (row_h - atlas.LineHeight()) * 0.5f;
            if (off_default) {
                DrawBoldText(canvas, atlas, row_x + kContentPadding, label_y, row.label,
                            label_color);
            } else {
                DrawText(canvas, atlas, row_x + kContentPadding, label_y, row.label,
                        label_color);
            }

            if (IsGyroRow(row.item)) {
                const float bars_right = row_x + row_w - kContentPadding;
                const float y_bar_x = bars_right - kGyroBarWidth;
                const float x_bar_x = y_bar_x - kContentPadding - kGyroBarWidth;
                const float bar_y = y + (row_h - kPercentBarHeight) * 0.5f;
                const std::string x_label =
                    "X " + std::to_string(model.settings.gyro_sensitivity_x) + "%";
                const std::string y_label =
                    "Y " + std::to_string(model.settings.gyro_sensitivity_y) + "%";
                DrawPercentBar(canvas, atlas, x_bar_x, bar_y, kGyroBarWidth, kPercentBarHeight,
                               (model.settings.gyro_sensitivity_x - 10) / (500.0f - 10.0f),
                               x_label, palette, armed && model.settings_gyro_axis == 0,
                               model.wave_elapsed * kWaveSpeed);
                DrawPercentBar(canvas, atlas, y_bar_x, bar_y, kGyroBarWidth, kPercentBarHeight,
                               (model.settings.gyro_sensitivity_y - 10) / (500.0f - 10.0f),
                               y_label, palette, armed && model.settings_gyro_axis == 1,
                               model.wave_elapsed * kWaveSpeed);
            } else if (IsPercentRow(row.item)) {
                const float bar_x = row_x + row_w - kContentPadding - kPercentBarWidth;
                const float bar_y = y + (row_h - kPercentBarHeight) * 0.5f;
                const float fraction =
                    row.item == SettingRowMovieThrottle
                        ? (model.settings.movie_throttle_clock_percentage - 10) / (100.0f - 10.0f)
                        : (model.settings.cpu_clock_percentage - 25) / (400.0f - 25.0f);
                DrawPercentBar(canvas, atlas, bar_x, bar_y, kPercentBarWidth, kPercentBarHeight,
                               fraction, row.value, palette, armed, model.wave_elapsed * kWaveSpeed);
            } else {
                const float value_width = MeasureText(atlas, canvas, row.value);
                const CanvasColor value_color = armed ? palette.accent : palette.text_dim;
                DrawText(canvas, atlas, row_x + row_w - kContentPadding - value_width,
                        label_y, row.value, value_color);
            }
        }

        y += row_h + kRowPadding;
    }

    canvas.ClearClipRect();

    DrawDescriptionPanel(canvas, atlas, model, dt, content_x, row_viewport_bottom + kDescriptionGap,
                        row_w, kDescriptionHeight, selected_row.description);

    if (tapped_row >= 0) {
        const SettingRow& tapped = rows[static_cast<std::size_t>(tapped_row)];
        if (model.settings_row != tapped_row) {
            model.settings_row = tapped_row;
            model.settings_row_armed = false;
        }
        if (IsBooleanSetting(tapped.item)) {
            activate_row(tapped);
        } else if (IsGyroRow(tapped.item) || IsCyclableRow(tapped.item)) {
            if (model.settings_row_armed) {
                deactivate_row(tapped);
            } else {
                activate_row(tapped);
            }
        }
    }

    ApplyRailFocusHints();
}

} // namespace SwitchFrontend
