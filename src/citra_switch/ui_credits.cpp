// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_credits.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_navbar.h"
#include "citra_switch/ui_page_transition.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr float kOutlineWidth = 2.0f;
constexpr float kHeaderIconSize = 64.0f;
constexpr float kTitleScale = 1.6f;
constexpr float kNameScale = 1.35f;
constexpr float kEyebrowScale = 0.8f;
constexpr float kEntryPaddingY = 22.0f;
constexpr float kEntrySpacing = 16.0f;
constexpr float kLineGap = 6.0f;

struct CreditEntry {
    std::string eyebrow;
    std::string name;
    std::string subtitle;
};

std::vector<CreditEntry> BuildCreditEntries() {
    return {
        {Tr("credits.eyebrow.built_on"), "Azahar Team", Tr("credits.subtitle.azahar")},
        {Tr("credits.eyebrow.port_team"), "Raibatsu", ""},
        {Tr("credits.eyebrow.port_team"), "Tico Azahar", ""},
        {Tr("credits.eyebrow.port_team"), "Dekopon", ""},
        {Tr("credits.eyebrow.resources"), "Google Fonts", Tr("credits.subtitle.google_fonts")},
        {Tr("credits.eyebrow.resources"), "GameTDB", Tr("credits.subtitle.gametdb")},
        {Tr("credits.eyebrow.resources"), "Lucide", Tr("credits.subtitle.lucide")},
        {Tr("credits.eyebrow.resources"), "Xelu's Prompts", Tr("credits.subtitle.xelu")},
        {Tr("credits.eyebrow.qa"), Tr("credits.testers"), Tr("credits.subtitle.testers")},
        {"", Tr("credits.you"), Tr("credits.subtitle.you")},
    };
}

void DrawCard(GpuCanvas& canvas, const UiPalette& palette, float x, float y, float w, float h) {
    canvas.DrawQuad(x - kOutlineWidth, y - kOutlineWidth, w + kOutlineWidth * 2.0f,
                    h + kOutlineWidth * 2.0f, palette.text_dim);
    canvas.DrawQuad(x, y, w, h, palette.surface);
}

void DrawBoldText(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float y, const std::string& text,
                  CanvasColor color, float scale) {
    DrawText(canvas, atlas, x, y, text, color, scale);
    DrawText(canvas, atlas, x + 1.0f, y, text, color, scale);
}

void DrawCentered(GpuCanvas& canvas, GlyphAtlas& atlas, float center_x, float y,
                  const std::string& text, CanvasColor color, float scale, bool bold) {
    const float width = MeasureText(atlas, canvas, text, scale);
    const float x = center_x - width * 0.5f;
    if (bold) {
        DrawBoldText(canvas, atlas, x, y, text, color, scale);
    } else {
        DrawText(canvas, atlas, x, y, text, color, scale);
    }
}

float EntryHeight(GlyphAtlas& atlas, const CreditEntry& entry) {
    float h = kEntryPaddingY * 2.0f;
    if (!entry.eyebrow.empty()) {
        h += atlas.LineHeight() * kEyebrowScale + kLineGap;
    }
    h += atlas.LineHeight() * kNameScale;
    if (!entry.subtitle.empty()) {
        h += kLineGap + atlas.LineHeight();
    }
    return h;
}

} // namespace

void UpdateCredits(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                   float dt) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const bool was_rail_focused = model.rail_focused;
    if (!model.rail_focused && (input.cancel || input.rail_prev || input.rail_next)) {
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
        model.active_rail = RailItem::Library;
        model.focus = LibraryFocus::List;
        model.rail_focused = false;
    } else if (tapped_rail == static_cast<int>(RailItem::Settings)) {
        model.active_rail = RailItem::Settings;
        model.rail_focused = false;
    } else if (tapped_rail == static_cast<int>(RailItem::Credits)) {
        model.rail_focused = false;
    }

    if (model.active_rail != RailItem::Credits) {
        return;
    }

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
    const float content_w = screen_w - content_x - kContentPadding;

    const std::vector<CreditEntry> entries = BuildCreditEntries();
    if (model.credits_row >= static_cast<int>(entries.size())) {
        model.credits_row = static_cast<int>(entries.size()) - 1;
    }

    const MenuInput kNoInput{};
    const MenuInput& content_input = (model.rail_focused || was_rail_focused) ? kNoInput : input;

    if (model.rail_focused) {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::RailPrev), Tr("hint.prev")};
        model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::RailNext), Tr("hint.next")};
        model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.select")};
        model.hint_chip_count = 3;
    } else {
        model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.rail")};
        model.hint_chip_count = 1;
    }

    float header_y = kContentPadding;
    const AtlasRegion header_icon = GetIconRegion(Icon::Credits);
    if (header_icon.u1 > header_icon.u0) {
        const float icon_x = content_x + (content_w - kHeaderIconSize) * 0.5f;
        canvas.DrawTexturedQuad(icon_x, header_y, kHeaderIconSize, kHeaderIconSize, header_icon.u0,
                                header_icon.v0, header_icon.u1, header_icon.v1, palette.accent);
        header_y += kHeaderIconSize + kContentPadding * 0.5f;
    }
    DrawCentered(canvas, atlas, content_x + content_w * 0.5f, header_y, Tr("credits.title"),
                palette.text, kTitleScale, true);
    header_y += atlas.LineHeight() * kTitleScale + kContentPadding;

    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;
    const float viewport_top = header_y;

    if (!model.rail_focused) {
        if (content_input.up && model.credits_row > 0) {
            --model.credits_row;
        }
        if (content_input.down && model.credits_row + 1 < static_cast<int>(entries.size())) {
            ++model.credits_row;
        }
    }

    float selected_top = viewport_top;
    float selected_height = 0.0f;
    float content_height = 0.0f;
    {
        float y = viewport_top;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            const float h = EntryHeight(atlas, entries[static_cast<std::size_t>(i)]);
            if (i == model.credits_row) {
                selected_top = y;
                selected_height = h;
            }
            y += h + kEntrySpacing;
            content_height = y - viewport_top - kEntrySpacing;
        }
    }

    UpdateScroll(model.credits_scroll, content_input, model.credits_row, selected_top,
                selected_height, content_x, viewport_top, content_w,
                viewport_bottom - viewport_top, content_height - (viewport_bottom - viewport_top));
    const float scroll_offset = model.credits_scroll.offset;

    canvas.SetClipRect(content_x - kOutlineWidth, viewport_top - kOutlineWidth,
                       content_w + kOutlineWidth * 2.0f,
                       viewport_bottom - viewport_top + kOutlineWidth);
    float row_y = viewport_top - scroll_offset;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const CreditEntry& entry = entries[static_cast<std::size_t>(i)];
        const float h = EntryHeight(atlas, entry);
        const bool selected = i == model.credits_row && !model.rail_focused;
        if (row_y + h >= viewport_top && row_y <= viewport_bottom) {
            DrawCard(canvas, palette, content_x, row_y, content_w, h);
            if (selected) {
                canvas.DrawQuad(content_x, row_y, content_w, h, palette.surface_warm);
            }
            float text_y = row_y + kEntryPaddingY;
            const float center_x = content_x + content_w * 0.5f;
            if (!entry.eyebrow.empty()) {
                DrawCentered(canvas, atlas, center_x, text_y, entry.eyebrow, palette.accent,
                            kEyebrowScale, false);
                text_y += atlas.LineHeight() * kEyebrowScale + kLineGap;
            }
            DrawCentered(canvas, atlas, center_x, text_y, entry.name, palette.text, kNameScale,
                        true);
            text_y += atlas.LineHeight() * kNameScale;
            if (!entry.subtitle.empty()) {
                text_y += kLineGap;
                DrawCentered(canvas, atlas, center_x, text_y, entry.subtitle, palette.text_dim,
                            1.0f, false);
            }

            if (content_input.touch_tap && content_input.touch_x >= content_x &&
                content_input.touch_x < content_x + content_w && content_input.touch_y >= row_y &&
                content_input.touch_y < row_y + h) {
                model.credits_row = i;
            }
        }
        row_y += h + kEntrySpacing;
    }
    canvas.ClearClipRect();
}

} // namespace SwitchFrontend
