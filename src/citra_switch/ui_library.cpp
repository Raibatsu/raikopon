// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_library.h"

#include <cmath>
#include <cstddef>

#include "citra_switch/game_settings.h"
#include "citra_switch/ui_game_icons.h"
#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_navbar.h"
#include "citra_switch/ui_page_transition.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 56.0f;
constexpr float kRowPadding = 8.0f;
constexpr float kRowIconSize = 40.0f;
constexpr float kRowInnerPadding = 8.0f;
constexpr float kRowIconGap = 12.0f;
constexpr float kRowIconOutline = 2.0f;
constexpr float kRailAnimRate = 10.0f;

} // namespace

void UpdateLibrary(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                   float dt) {
    if (!model.games_scanned) {
        model.games = ScanGames();
        for (GameEntry& entry : model.games) {
            const PlaytimeRecord record = LoadPlaytime(entry.program_id);
            entry.total_playtime_seconds = record.total_seconds;
            entry.last_played = record.last_played;
        }
        model.games_scanned = true;
        if (model.selected_game >= static_cast<int>(model.games.size())) {
            model.selected_game = 0;
        }
    }
    // Deliberately not folded into the `!games_scanned` gate above - `canvas`/`atlas` are rebuilt
    // fresh every time RunLauncher() runs (e.g. on returning from a game), but `games_scanned`
    // stays true for the whole app session, so the icons need their own reload trigger
    // (`game_icons_loaded`, reset by RunLauncher() at the top of every fresh session) or they'd
    // keep pointing into an atlas that no longer has them.
    if (!model.game_icons_loaded) {
        LoadGameIcons(canvas, atlas, model.games);
        model.game_icons_loaded = true;
    }

    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const bool notice_visible = !model.notice_text.empty();
    if (notice_visible) {
        canvas.DrawQuad(kRailCollapsedWidth, 0.0f, screen_w - kRailCollapsedWidth, kBannerHeight,
                        palette.surface_warm);
        const std::string banner_text =
            model.notice_is_error ? Tr("gamedetails.error_prefix") + model.notice_text : model.notice_text;
        DrawText(canvas, atlas, kRailContentX, (kBannerHeight - atlas.LineHeight()) * 0.5f,
                banner_text, palette.accent);

        const bool any_press = input.up || input.down || input.left || input.right ||
                               input.confirm || input.cancel || input.minus || input.plus ||
                               input.touch_tap;
        if (any_press) {
            model.notice_text.clear();
            model.notice_is_error = false;
            return;
        }
    }

    // The rail is reachable directly via ZL/ZR (RailPrev/RailNext), or by backing all the way out
    // (Cancel) from the outermost level of whatever's currently showing - but not via Left, which
    // Settings' cyclable rows need for adjusting Internal Resolution, Texture Filter, etc. Once
    // focused, Up/Down and ZL/ZR both cycle the (now 3) rail items.
    if (input.rail_prev || input.rail_next) {
        model.focus = LibraryFocus::Rail;
    }
    if (model.focus == LibraryFocus::Rail) {
        int rail_index = static_cast<int>(model.active_rail);
        if (input.up || input.rail_prev) {
            rail_index = (rail_index + kNumRailItems - 1) % kNumRailItems;
        }
        if (input.down || input.rail_next) {
            rail_index = (rail_index + 1) % kNumRailItems;
        }
        const RailItem new_rail = static_cast<RailItem>(rail_index);
        if (new_rail != model.active_rail) {
            if (new_rail == RailItem::Settings) {
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
            model.focus = LibraryFocus::List;
        } else if (input.right && model.active_rail != RailItem::Exit) {
            model.focus = LibraryFocus::List;
        } else if (input.cancel && model.active_rail == RailItem::Exit) {
            TriggerPageTransition(model, canvas, atlas, RailItem::Library, /*commit=*/false);
            return;
        }
    } else if (model.active_rail == RailItem::Library) {
        if (input.cancel) {
            model.focus = LibraryFocus::Rail;
            return;
        }
        if (input.minus) {
            model.install_open = true;
            model.install_listed = false;
            return;
        }
        if (!model.games.empty()) {
            if (input.up && model.selected_game > 0) {
                --model.selected_game;
            }
            if (input.down && model.selected_game + 1 < static_cast<int>(model.games.size())) {
                ++model.selected_game;
            }
            if (input.confirm) {
                model.details_action = 0;
                model.library_panel = LibraryPanel::Details;
            }
        }
    }

    if (model.focus == LibraryFocus::Rail) {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::RailPrev), Tr("hint.prev")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::RailNext), Tr("hint.next")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.select")};
        model.hint_chip_count = 3;
    } else {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.open")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Minus), Tr("hint.install")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.rail")};
        model.hint_chip_count = 3;
    }

    model.rail_focused = model.focus == LibraryFocus::Rail;

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
        rail_target + (model.rail_pop_anim - rail_target) * std::exp(-dt * kRailAnimRate);

    const int tapped_rail =
        DrawNavBar(canvas, atlas, rail_items, kNumRailItems, static_cast<int>(model.active_rail),
                  0.0f, screen_h * 0.5f, kRailCollapsedWidth, kRailItemHeight, true, input,
                  model.wave_elapsed);
    if (tapped_rail == static_cast<int>(RailItem::Exit)) {
        model.exit_requested = true;
        return;
    } else if (tapped_rail == static_cast<int>(RailItem::Library)) {
        model.active_rail = RailItem::Library;
        model.focus = LibraryFocus::List;
        model.rail_focused = false;
    } else if (tapped_rail == static_cast<int>(RailItem::Settings)) {
        TriggerPageTransition(model, canvas, atlas, RailItem::Settings);
        return;
    } else if (tapped_rail == static_cast<int>(RailItem::Credits)) {
        model.active_rail = RailItem::Credits;
        model.rail_focused = false;
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
    const float content_top = kContentPadding + (notice_visible ? kBannerHeight : 0.0f);

    if (model.active_rail != RailItem::Library) {
        return;
    }

    if (model.games.empty()) {
        DrawText(canvas, atlas, content_x, content_top, Tr("library.no_games"), palette.text_dim);
        return;
    }

    const float row_w = screen_w - content_x - kContentPadding;
    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;
    const bool selected_grown = model.focus == LibraryFocus::List;
    const float selected_top =
        content_top + static_cast<float>(model.selected_game) * (kRowHeight + kRowPadding);
    const float selected_height = selected_grown ? kRowHeight * kSelectedGrowth : kRowHeight;
    const float content_height =
        static_cast<float>(model.games.size()) * (kRowHeight + kRowPadding);
    UpdateScroll(model.library_scroll, input, model.selected_game, selected_top, selected_height,
                content_x, content_top, row_w, viewport_bottom - content_top,
                content_height - (viewport_bottom - content_top));
    const float scroll_offset = model.library_scroll.offset;

    canvas.SetClipRect(content_x, content_top, row_w, viewport_bottom - content_top);

    int tapped_row = -1;
    float row_y = content_top - scroll_offset;
    for (std::size_t i = 0; i < model.games.size(); ++i) {
        const bool selected = static_cast<int>(i) == model.selected_game;
        float row_h = kRowHeight;
        if (selected && model.focus == LibraryFocus::List) {
            row_h *= kSelectedGrowth;
        }

        if (row_y + row_h >= content_top && row_y <= viewport_bottom) {
            if (selected) {
                canvas.DrawQuad(content_x, row_y, row_w, row_h, palette.surface_warm);
            }

            if (input.touch_tap && input.touch_x >= content_x &&
                input.touch_x < content_x + row_w && input.touch_y >= row_y &&
                input.touch_y < row_y + row_h) {
                tapped_row = static_cast<int>(i);
            }

            const AtlasRegion icon_region = GetGameIconRegion(i);
            if (icon_region.u1 > icon_region.u0) {
                const float icon_x = content_x + kRowInnerPadding;
                const float icon_y = row_y + (row_h - kRowIconSize) * 0.5f;
                canvas.DrawQuad(icon_x - kRowIconOutline, icon_y - kRowIconOutline,
                                kRowIconSize + kRowIconOutline * 2.0f,
                                kRowIconSize + kRowIconOutline * 2.0f, palette.accent);
                canvas.DrawTexturedQuad(icon_x, icon_y, kRowIconSize, kRowIconSize, icon_region.u0,
                                        icon_region.v0, icon_region.u1, icon_region.v1,
                                        CanvasColor{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, true);
            }

            const float text_x = content_x + kRowInnerPadding + kRowIconSize + kRowIconGap;
            const float text_y = row_y + (row_h - atlas.LineHeight()) * 0.5f;
            DrawText(canvas, atlas, text_x, text_y, model.games[i].title,
                    selected ? palette.text : palette.text_dim);
        }

        row_y += row_h + kRowPadding;
    }

    canvas.ClearClipRect();

    if (tapped_row >= 0) {
        model.selected_game = tapped_row;
        model.focus = LibraryFocus::List;
        model.details_action = 0;
        model.library_panel = LibraryPanel::Details;
    }
}

} // namespace SwitchFrontend
