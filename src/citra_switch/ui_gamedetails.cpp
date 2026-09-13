// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_gamedetails.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "citra_switch/game_settings.h"
#include "citra_switch/gametdb.h"
#include "citra_switch/layout_editor.h"
#include "citra_switch/library_cheats.h"
#include "citra_switch/library_mods.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/titledb.h"
#include "citra_switch/ui_game_icons.h"
#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_navbar.h"
#include "citra_switch/ui_page_transition.h"
#include "citra_switch/ui_scroll.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"
#include "common/logging/log.h"

namespace SwitchFrontend {

namespace {

constexpr float kHeaderTitleScale = 1.6f;
constexpr float kOutlineWidth = 2.0f;
constexpr float kColumnGap = 32.0f;
constexpr float kLeftColumnFraction = 0.58f;

constexpr float kActionHeight = 52.0f;
constexpr float kActionSpacing = 10.0f;
constexpr float kTextLineGap = 4.0f;
constexpr float kSectionGap = 20.0f;
constexpr float kCardPadding = 14.0f;
constexpr float kDetailRowGap = 14.0f;
constexpr float kDetailDividerHeight = 1.0f;

// Not stored in titledb.bin (would repeat this whole sentence 4785 times for no reason) - added
// here in code instead, shown under the description whenever we're displaying anything sourced
// from GameTDB (synopsis or the Details card's fields).
constexpr const char* kGameTdbAttribution =
    "© 2009-2026 GameTDB - This data is provided through GameTDB. All rights reserved. "
    "Please visit https://www.gametdb.com for more information. They're an awesome bunch!";

constexpr int kMaxRecommended = 4;
constexpr float kRecRowHeight = 44.0f;
constexpr float kRecRowSpacing = 6.0f;
constexpr float kRecIconSize = 30.0f;
constexpr float kRecIconOutline = 2.0f;
constexpr float kRecIconGap = 10.0f;
constexpr float kRecInnerPadding = 8.0f;

void DrawBoldText(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float y, const std::string& text,
                  CanvasColor color, float scale = 1.0f) {
    DrawText(canvas, atlas, x, y, text, color, scale);
    DrawText(canvas, atlas, x + 1.0f, y, text, color, scale);
}

// "Developer  •  Publisher" - whichever of the two is present, skipping the separator (and the
// whole line) if only one is, or neither. Prefers GameTDB's own publisher over the SMDH-derived
// entry.publisher when both exist, same preference BuildDetailRows uses.
std::string BuildSubheading(const GameEntry& entry, const std::optional<TitleInfo>& info) {
    const std::string developer = info ? std::string(info->developer) : std::string{};
    const std::string publisher =
        info && !info->publisher.empty() ? std::string(info->publisher) : entry.publisher;
    if (!developer.empty() && !publisher.empty() && developer != publisher) {
        return developer + "   •   " + publisher;
    }
    if (!developer.empty()) {
        return developer;
    }
    return publisher;
}

CanvasColor AverageIconColor(const GameEntry& entry) {
    if (entry.icon.empty()) {
        return CurrentPalette().surface_warm;
    }
    std::uint64_t r = 0;
    std::uint64_t g = 0;
    std::uint64_t b = 0;
    for (std::uint32_t pixel : entry.icon) {
        r += pixel & 0xFF;
        g += (pixel >> 8) & 0xFF;
        b += (pixel >> 16) & 0xFF;
    }
    const float count = static_cast<float>(entry.icon.size());
    return CanvasColor{static_cast<float>(r) / count / 255.0f,
                       static_cast<float>(g) / count / 255.0f,
                       static_cast<float>(b) / count / 255.0f, 1.0f};
}

std::string FormatPlaytime(const GameEntry& entry) {
    if (entry.last_played == 0) {
        return Tr("gamedetails.never_played");
    }
    const std::uint64_t hours = entry.total_playtime_seconds / 3600;
    const std::uint64_t minutes = (entry.total_playtime_seconds % 3600) / 60;
    if (hours > 0) {
        return fmt::format("{}h {}m {}", hours, minutes, Tr("gamedetails.played_suffix"));
    }
    return fmt::format("{}m {}", minutes, Tr("gamedetails.played_suffix"));
}

// "EN,FR,DE" -> "EN, FR, DE" - GameTDB's raw comma lists have no spacing, only readable once
// word-wrapped if there's a break opportunity after each comma.
std::string PrettyCommaList(std::string_view csv) {
    std::string out;
    out.reserve(csv.size() + csv.size() / 3);
    for (char c : csv) {
        out += c;
        if (c == ',') {
            out += ' ';
        }
    }
    return out;
}

// "role-playing" -> "Role-Playing" - GameTDB's genre/feature tags come through as all-lowercase
// prose fragments; capitalized here for display only, never touches the stored data.
std::string TitleCase(std::string_view text) {
    std::string out(text);
    bool cap_next = true;
    for (char& c : out) {
        if (cap_next && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        cap_next = (c == ' ' || c == ',' || c == '-');
    }
    return out;
}

std::string FormatDate(const TitleInfo& info) {
    static constexpr std::array<const char*, 13> kMonths = {
        "",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    if (info.release_month >= 1 && info.release_month <= 12 && info.release_day > 0) {
        return fmt::format("{} {}, {}", kMonths[static_cast<std::size_t>(info.release_month)],
                           info.release_day, info.release_year);
    }
    return fmt::format("{}", info.release_year);
}

std::string FormatRating(const TitleInfo& info) {
    std::string out;
    if (!info.rating_type.empty()) {
        out += std::string(info.rating_type);
    }
    if (!info.rating_value.empty()) {
        if (!out.empty()) {
            out += " ";
        }
        out += std::string(info.rating_value);
    }
    if (out.empty()) {
        out = Tr("gamedetails.unrated");
    }
    if (!info.rating_descriptors.empty()) {
        out += fmt::format(" ({})", info.rating_descriptors);
    }
    return out;
}

std::string FormatWifi(const TitleInfo& info) {
    if (info.wifi_players <= 0 && info.wifi_features.empty()) {
        return Tr("common.none");
    }
    std::string out;
    if (info.wifi_players > 0) {
        out = fmt::format("{} {}", info.wifi_players, Tr("gamedetails.players_suffix"));
    }
    if (!info.wifi_features.empty()) {
        if (!out.empty()) {
            out += ", ";
        }
        out += TitleCase(PrettyCommaList(info.wifi_features));
    }
    return out;
}

// Accent label + thin underline rule, same idiom as ui_controls.cpp/ui_settings.cpp's tab-group
// headers. Returns the y position content below the heading should start at.
float DrawSectionHeading(GpuCanvas& canvas, GlyphAtlas& atlas, const UiPalette& palette, float x,
                         float y, float w, const std::string& text) {
    DrawText(canvas, atlas, x, y, text, palette.accent);
    const float underline_y = y + atlas.LineHeight() + 2.0f;
    canvas.DrawQuad(x, underline_y, w, kHeaderUnderlineHeight, palette.accent);
    return underline_y + kHeaderUnderlineHeight + kContentPadding * 0.5f;
}

float SectionHeadingHeight(GlyphAtlas& atlas) {
    return atlas.LineHeight() + 2.0f + kHeaderUnderlineHeight + kContentPadding * 0.5f;
}

// A 2px-outlined "card" background - a slightly larger quad in an outline colour behind a smaller
// fill quad, the same trick the hold-progress ring and in-game overlays use.
void DrawCard(GpuCanvas& canvas, const UiPalette& palette, float x, float y, float w, float h) {
    canvas.DrawQuad(x - kOutlineWidth, y - kOutlineWidth, w + kOutlineWidth * 2.0f,
                    h + kOutlineWidth * 2.0f, palette.text_dim);
    canvas.DrawQuad(x, y, w, h, palette.surface);
}

// A label (its own short line, small/accent so it reads as a field name, not prose) plus a
// wrapped value (brighter, the actual content) - visually distinct rows instead of one flowing
// "Label: value, Label: value..." paragraph.
struct DetailRow {
    std::string label;
    std::vector<std::string> value_lines;
    float height = 0.0f;
};

std::vector<DetailRow> BuildDetailRows(GpuCanvas& canvas, GlyphAtlas& atlas,
                                       const GameEntry& entry,
                                       const std::optional<TitleInfo>& info, TitleKind kind,
                                       float max_width) {
    std::vector<DetailRow> rows;
    const auto add = [&](const std::string& label, const std::string& value) {
        if (value.empty()) {
            return;
        }
        DetailRow row;
        row.label = label;
        row.value_lines = WrapText(canvas, atlas, value, max_width);
        row.height = atlas.LineHeight() * (1.0f + static_cast<float>(row.value_lines.size())) +
                    kDetailRowGap;
        rows.push_back(std::move(row));
    };

    add(Tr("gamedetails.detail.type"), TitleKindName(kind));
    add(Tr("gamedetails.detail.time_spent"), FormatPlaytime(entry));

    // Developer/publisher deliberately not repeated here - they're already shown as the header's
    // subheading (see BuildSubheading), right under the title.
    if (info) {
        add(Tr("gamedetails.detail.genre"), TitleCase(PrettyCommaList(info->genre)));
        add(Tr("gamedetails.detail.released"), info->release_year > 0 ? FormatDate(*info) : "");
        add(Tr("gamedetails.detail.region"), std::string(info->region));
        add(Tr("gamedetails.detail.languages"), PrettyCommaList(info->languages));
        add(Tr("gamedetails.detail.rating"),
            (!info->rating_type.empty() || !info->rating_value.empty()) ? FormatRating(*info) : "");
        add(Tr("gamedetails.detail.wifi"),
            (info->wifi_players > 0 || !info->wifi_features.empty()) ? FormatWifi(*info) : "");
        add(Tr("gamedetails.detail.players"),
            info->input_players > 0 ? fmt::format("{}", info->input_players) : "");
    }
    return rows;
}

} // namespace

void UpdateGameDetails(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                       GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);

    const NavItem rail_items[] = {{Tr("nav.library"), GetIconRegion(Icon::Library)},
                                  {Tr("nav.settings"), GetIconRegion(Icon::Settings)},
                                  {Tr("nav.credits"), GetIconRegion(Icon::Credits)},
                                  {Tr("nav.exit"), GetIconRegion(Icon::Exit)}};
    // This screen has no rail-focus mechanic of its own (Cancel always just goes back to the
    // list), so the rail simply stays in whatever popped/retracted state it was already in from
    // Library, not force itself fully retracted every time.
    const int tapped_rail = DrawNavBar(canvas, atlas, rail_items, kNumRailItems,
                                       static_cast<int>(RailItem::Library), 0.0f, screen_h * 0.5f,
                                       kRailCollapsedWidth, kRailItemHeight, true, input,
                                       model.wave_elapsed);
    if (tapped_rail == static_cast<int>(RailItem::Exit)) {
        model.exit_requested = true;
        return;
    }
    if (tapped_rail == static_cast<int>(RailItem::Library)) {
        model.library_panel = LibraryPanel::List;
        return;
    }
    if (tapped_rail == static_cast<int>(RailItem::Settings)) {
        TriggerPageTransition(model, canvas, atlas, RailItem::Settings);
        return;
    }
    if (tapped_rail == static_cast<int>(RailItem::Credits)) {
        model.library_panel = LibraryPanel::List;
        model.active_rail = RailItem::Credits;
        model.rail_focused = false;
        return;
    }

    if (model.games.empty() || model.selected_game < 0 ||
        model.selected_game >= static_cast<int>(model.games.size())) {
        model.library_panel = LibraryPanel::List;
        return;
    }

    // Draws the active rail item's bookmark tab (if popped) on top of whatever content this
    // frame draws below, no matter which of this function's several return paths gets hit.
    const NavBarActiveTabGuard active_tab_guard{canvas,
                                                atlas,
                                                rail_items,
                                                kNumRailItems,
                                                static_cast<int>(RailItem::Library),
                                                0.0f,
                                                screen_h * 0.5f,
                                                kRailCollapsedWidth,
                                                kRailItemHeight,
                                                model.rail_pop_anim};

    const float content_x = kRailContentX;

    const bool notice_visible = !model.notice_text.empty();
    if (notice_visible) {
        canvas.DrawQuad(kRailCollapsedWidth, 0.0f, screen_w - kRailCollapsedWidth, kBannerHeight,
                        palette.surface_warm);
        const std::string banner_text =
            model.notice_is_error ? Tr("gamedetails.error_prefix") + model.notice_text : model.notice_text;
        DrawText(canvas, atlas, content_x, (kBannerHeight - atlas.LineHeight()) * 0.5f,
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

    if (input.cancel) {
        model.library_panel = LibraryPanel::List;
        return;
    }

    // Other games shown as clickable cards below the actions - up to kMaxRecommended, in Library
    // order, skipping the game currently being viewed. Keyboard selection (details_action) spans
    // both the visible actions above (4 collapsed, 7 expanded) and these cards as one contiguous
    // list, so Up/Down can reach either.
    std::vector<std::size_t> recommended;
    for (std::size_t i = 0;
        i < model.games.size() && recommended.size() < static_cast<std::size_t>(kMaxRecommended);
        ++i) {
        if (static_cast<int>(i) != model.selected_game) {
            recommended.push_back(i);
        }
    }
    // Play/Settings/Layout always show; Clear Cache/Cheats/Mods collapse behind a "More Actions" /
    // "Fewer Actions" toggle at index 3 (stable across fold/unfold) so the action list doesn't
    // crowd out the cover art and description for the common case where nobody needs them.
    constexpr int kActionPlay = 0;
    constexpr int kActionSettings = 1;
    constexpr int kActionLayout = 2;
    constexpr int kActionMore = 3;
    constexpr int kActionClearCache = 4;
    constexpr int kActionCheats = 5;
    constexpr int kActionMods = 6;

    std::vector<int> action_kinds{kActionPlay, kActionSettings, kActionLayout, kActionMore};
    if (model.details_actions_expanded) {
        action_kinds.push_back(kActionClearCache);
        action_kinds.push_back(kActionCheats);
        action_kinds.push_back(kActionMods);
    }
    const int visible_action_count = static_cast<int>(action_kinds.size());
    const int total_items = visible_action_count + static_cast<int>(recommended.size());

    if (input.up && model.details_action > 0) {
        --model.details_action;
    }
    if (input.down && model.details_action + 1 < total_items) {
        ++model.details_action;
    }

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.select")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
    model.hint_chip_count = 2;

    const GameEntry& entry = model.games[static_cast<std::size_t>(model.selected_game)];
    const TitleKind kind = ClassifyTitle(entry.program_id);
    const std::optional<TitleInfo> title_info =
        GetTitleInfo(GameTdbGameId(entry.product_code), model.settings.language);

    const float content_w = screen_w - content_x - kContentPadding;
    const float y = kContentPadding + (notice_visible ? kBannerHeight : 0.0f);

    const float left_w = std::round(content_w * kLeftColumnFraction);
    const float right_x = content_x + left_w + kColumnGap;
    const float right_w = content_w - left_w - kColumnGap;

    // --- Header: title (biggest), developer/publisher subheading, then the action buttons, all
    // fixed (not part of the scrollable content below - they're always on screen). The cover art
    // fills the entire right side at the same height, aspect-fit and centered, using whatever
    // language best matches the console's System Language (see GetCoverRegion) - a flat
    // average-icon-colour card shows through anywhere the cover doesn't cover it, or on its own
    // if nothing's been downloaded for this title yet (Settings > System > Download All Covers).
    const std::string subheading = BuildSubheading(entry, title_info);
    const float title_h = atlas.LineHeight() * kHeaderTitleScale;
    const float subheading_h = subheading.empty() ? 0.0f : atlas.LineHeight() + kTextLineGap;
    const float actions_block_h =
        static_cast<float>(visible_action_count) * (kActionHeight + kActionSpacing) - kActionSpacing;
    const float header_h =
        title_h + subheading_h + kContentPadding * 0.5f + kContentPadding * 0.5f + actions_block_h;
    // The cover art card is pinned to the collapsed (4-action) height always, regardless of
    // visible_action_count - it and its outline shouldn't stretch/shrink every time "More
    // Actions" is toggled. Matches header_h exactly in the default collapsed state (no visible
    // seam); expanding just lets the action list run a bit past the bottom of a now-fixed-size
    // cover instead of growing it.
    constexpr int kCollapsedActionCount = 4;
    const float collapsed_actions_block_h =
        static_cast<float>(kCollapsedActionCount) * (kActionHeight + kActionSpacing) - kActionSpacing;
    const float cover_box_h =
        title_h + subheading_h + kContentPadding * 0.5f + kContentPadding * 0.5f + collapsed_actions_block_h;

    float header_y = y;
    DrawBoldText(canvas, atlas, content_x, header_y, entry.title, palette.text, kHeaderTitleScale);
    header_y += title_h;
    if (!subheading.empty()) {
        DrawText(canvas, atlas, content_x, header_y, subheading, palette.text_dim);
        header_y += subheading_h;
    }
    header_y += kContentPadding * 0.5f;

    const std::string kind_labels[7] = {
        Tr("gamedetails.action.play"),
        Tr("gamedetails.action.settings"),
        Tr("gamedetails.action.layout"),
        model.details_actions_expanded ? Tr("gamedetails.action.less") : Tr("gamedetails.action.more"),
        Tr("gamedetails.action.clear_cache"),
        Tr("gamedetails.action.cheats"),
        Tr("gamedetails.action.mods"),
    };
    int touched_action = -1;
    for (int i = 0; i < visible_action_count; ++i) {
        const bool selected = model.details_action == i;
        const float row_h = selected ? kActionHeight * kSelectedGrowth : kActionHeight;
        DrawCard(canvas, palette, content_x, header_y, left_w, row_h);
        canvas.DrawQuad(content_x, header_y, left_w, row_h,
                        selected ? palette.surface_warm : palette.surface);
        const CanvasColor label_color = selected ? palette.text : palette.text_dim;
        DrawText(canvas, atlas, content_x + kContentPadding,
                header_y + (row_h - atlas.LineHeight()) * 0.5f,
                kind_labels[static_cast<std::size_t>(action_kinds[static_cast<std::size_t>(i)])],
                label_color);

        if (input.touch_tap && input.touch_x >= content_x && input.touch_x < content_x + left_w &&
            input.touch_y >= header_y && input.touch_y < header_y + row_h) {
            touched_action = i;
        }

        header_y += row_h + kActionSpacing;
    }

    DrawCard(canvas, palette, right_x, y, right_w, cover_box_h);
    canvas.DrawQuad(right_x, y, right_w, cover_box_h, AverageIconColor(entry));
    const std::optional<CoverArt> cover =
        GetCoverRegion(canvas, atlas, entry, model.settings.language);
    if (cover && cover->width > 0 && cover->height > 0) {
        const float box_aspect = right_w / cover_box_h;
        const float img_aspect =
            static_cast<float>(cover->width) / static_cast<float>(cover->height);
        float cover_w;
        float cover_h;
        if (img_aspect > box_aspect) {
            cover_h = cover_box_h;
            cover_w = cover_h * img_aspect;
        } else {
            cover_w = right_w;
            cover_h = cover_w / img_aspect;
        }
        const float cover_x = right_x + (right_w - cover_w) * 0.5f;
        const float cover_y = y + (cover_box_h - cover_h) * 0.5f;
        canvas.SetClipRect(right_x, y, right_w, cover_box_h);
        canvas.DrawTexturedQuad(cover_x, cover_y, cover_w, cover_h, cover->region.u0,
                                cover->region.v0, cover->region.u1, cover->region.v1,
                                CanvasColor{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, true);
        canvas.ClearClipRect();
    }

    const float content_top = y + header_h + kContentPadding;
    const float viewport_bottom = screen_h - kHintBarHeight - kContentPadding;

    // --- Left column: description only now - actions moved into the fixed header above. ---
    const std::string_view description = title_info ? title_info->synopsis : std::string_view{};
    const std::vector<std::string> desc_lines =
        description.empty() ? std::vector<std::string>{} : WrapText(canvas, atlas, description, left_w);
    const float desc_heading_h = SectionHeadingHeight(atlas);
    const float desc_block_h = desc_lines.empty()
                                  ? atlas.LineHeight()
                                  : static_cast<float>(desc_lines.size()) * atlas.LineHeight();

    const float left_column_h = desc_heading_h + desc_block_h;

    // --- Right column: details card, then recommendations. ---
    const std::vector<DetailRow> detail_rows =
        BuildDetailRows(canvas, atlas, entry, title_info, kind, right_w - kCardPadding * 2.0f);
    float detail_rows_h = 0.0f;
    for (const DetailRow& row : detail_rows) {
        detail_rows_h += row.height;
    }
    const float details_heading_h = SectionHeadingHeight(atlas);
    const float details_card_h = kCardPadding * 2.0f + detail_rows_h;

    const float rec_heading_h = SectionHeadingHeight(atlas);
    const float rec_block_h =
        static_cast<float>(recommended.size()) * (kRecRowHeight + kRecRowSpacing);

    const float right_column_h =
        details_heading_h + details_card_h + kSectionGap + rec_heading_h + rec_block_h;

    const float columns_h = std::max(left_column_h, right_column_h);

    // Credit GameTDB whenever any of its data is on screen - synopsis or the Details card's
    // fields both count, so this isn't gated on desc_lines specifically. Drawn as a full-width
    // footer below both columns (not squeezed into the left column's width like the description),
    // so it reads as a distinct closing line rather than part of the synopsis paragraph.
    const std::vector<std::string> attribution_lines =
        title_info ? WrapText(canvas, atlas, kGameTdbAttribution, content_w) : std::vector<std::string>{};
    const float attribution_block_h =
        attribution_lines.empty()
            ? 0.0f
            : kSectionGap + static_cast<float>(attribution_lines.size()) * atlas.LineHeight();

    const float content_height = columns_h + attribution_block_h;

    float selected_top;
    float selected_height;
    if (model.details_action < visible_action_count) {
        // Actions live in the fixed header now, always visible - pin the "selection" to the very
        // top of the scrollable region below so browsing them always rests the scroll at 0,
        // rather than trying (and failing) to scroll to a row that isn't part of this area.
        selected_top = content_top;
        selected_height = 0.0f;
    } else {
        const int rec_index = model.details_action - visible_action_count;
        selected_top = content_top + details_heading_h + details_card_h + kSectionGap +
                       rec_heading_h + static_cast<float>(rec_index) * (kRecRowHeight + kRecRowSpacing);
        selected_height = kRecRowHeight * kSelectedGrowth;
    }

    UpdateScroll(model.game_details_scroll, input, model.details_action, selected_top,
                selected_height, content_x, content_top, content_w, viewport_bottom - content_top,
                content_height - (viewport_bottom - content_top));
    const float scroll_offset = model.game_details_scroll.offset;

    // Widened by kOutlineWidth on every side so DrawCard's outline (which extends past its
    // content rect by kOutlineWidth) never gets sliced off by this clip - otherwise the left edge
    // of every card flush against content_x loses its outline entirely.
    const float clip_x = content_x - kOutlineWidth;
    const float clip_y = content_top - kOutlineWidth;
    const float clip_w = content_w + kOutlineWidth * 2.0f;
    const float clip_h = (viewport_bottom - content_top) + kOutlineWidth * 2.0f;
    canvas.SetClipRect(clip_x, clip_y, clip_w, clip_h);

    // --- Draw left column (description only - actions are in the fixed header). ---
    float left_y = content_top - scroll_offset;
    left_y = DrawSectionHeading(canvas, atlas, palette, content_x, left_y, left_w,
                                Tr("gamedetails.section.description"));
    if (desc_lines.empty()) {
        DrawText(canvas, atlas, content_x, left_y, Tr("gamedetails.no_description"),
                palette.text_dim);
        left_y += atlas.LineHeight();
    } else {
        for (const std::string& line : desc_lines) {
            if (!line.empty()) {
                DrawText(canvas, atlas, content_x, left_y, line, palette.text_dim);
            }
            left_y += atlas.LineHeight();
        }
    }
    // --- Draw right column. ---
    float right_y = content_top - scroll_offset;
    right_y = DrawSectionHeading(canvas, atlas, palette, right_x, right_y, right_w,
                                 Tr("gamedetails.section.details"));
    DrawCard(canvas, palette, right_x, right_y, right_w, details_card_h);
    float detail_y = right_y + kCardPadding;
    for (std::size_t i = 0; i < detail_rows.size(); ++i) {
        const DetailRow& row = detail_rows[i];
        DrawText(canvas, atlas, right_x + kCardPadding, detail_y, row.label, palette.accent_dim);
        detail_y += atlas.LineHeight();
        for (const std::string& line : row.value_lines) {
            DrawText(canvas, atlas, right_x + kCardPadding, detail_y, line, palette.text_dim);
            detail_y += atlas.LineHeight();
        }
        if (i + 1 < detail_rows.size()) {
            const float divider_y = detail_y + kDetailRowGap * 0.5f - kDetailDividerHeight * 0.5f;
            canvas.DrawQuad(right_x + kCardPadding, divider_y, right_w - kCardPadding * 2.0f,
                            kDetailDividerHeight,
                            CanvasColor{palette.text_dim.r, palette.text_dim.g, palette.text_dim.b,
                                       0.3f});
        }
        detail_y += kDetailRowGap;
    }
    right_y += details_card_h + kSectionGap;

    right_y = DrawSectionHeading(canvas, atlas, palette, right_x, right_y, right_w,
                                 Tr("gamedetails.section.recommendations"));

    int touched_rec = -1;
    for (std::size_t j = 0; j < recommended.size(); ++j) {
        const std::size_t game_index = recommended[j];
        const GameEntry& rec_entry = model.games[game_index];
        const int row_index = visible_action_count + static_cast<int>(j);
        const bool selected = model.details_action == row_index;
        const float row_h = selected ? kRecRowHeight * kSelectedGrowth : kRecRowHeight;

        DrawCard(canvas, palette, right_x, right_y, right_w, row_h);
        canvas.DrawQuad(right_x, right_y, right_w, row_h,
                        selected ? palette.surface_warm : palette.surface);

        const float icon_x = right_x + kRecInnerPadding;
        const float icon_y = right_y + (row_h - kRecIconSize) * 0.5f;
        const AtlasRegion icon_region = GetGameIconRegion(game_index);
        if (icon_region.u1 > icon_region.u0) {
            canvas.DrawQuad(icon_x - kRecIconOutline, icon_y - kRecIconOutline,
                            kRecIconSize + kRecIconOutline * 2.0f,
                            kRecIconSize + kRecIconOutline * 2.0f, palette.accent);
            canvas.DrawTexturedQuad(icon_x, icon_y, kRecIconSize, kRecIconSize, icon_region.u0,
                                    icon_region.v0, icon_region.u1, icon_region.v1,
                                    CanvasColor{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, true);
        } else {
            canvas.DrawQuad(icon_x, icon_y, kRecIconSize, kRecIconSize,
                            AverageIconColor(rec_entry));
        }

        const float text_x = right_x + kRecInnerPadding + kRecIconSize + kRecIconGap;
        const float text_w = right_x + right_w - kContentPadding * 0.5f - text_x;
        // Intersected with the outer content clip, not just this row's own bounds - a row
        // scrolled partway (or fully) above content_top would otherwise get a clip rect that
        // extends above the visible viewport, letting its title bleed into the title/hero area.
        const float clip_y0 = std::max(right_y, content_top);
        const float clip_y1 = std::min(right_y + row_h, viewport_bottom);
        if (clip_y1 > clip_y0) {
            canvas.SetClipRect(text_x, clip_y0, text_w, clip_y1 - clip_y0);
            DrawText(canvas, atlas, text_x, right_y + (row_h - atlas.LineHeight()) * 0.5f,
                    rec_entry.title, selected ? palette.text : palette.text_dim);
            canvas.SetClipRect(clip_x, clip_y, clip_w, clip_h);
        }

        if (input.touch_tap && input.touch_x >= right_x && input.touch_x < right_x + right_w &&
            input.touch_y >= right_y && input.touch_y < right_y + row_h) {
            touched_rec = static_cast<int>(j);
        }

        right_y += row_h + kRecRowSpacing;
    }

    // --- Footer: GameTDB attribution, full width, below both columns. ---
    if (!attribution_lines.empty()) {
        float footer_y = content_top - scroll_offset + columns_h + kSectionGap;
        for (const std::string& line : attribution_lines) {
            if (!line.empty()) {
                CanvasColor dim_text_dim = palette.text_dim;
                dim_text_dim.a *= 0.7f;
                DrawText(canvas, atlas, content_x, footer_y, line, dim_text_dim);
            }
            footer_y += atlas.LineHeight();
        }
    }

    canvas.ClearClipRect();

    const int activated_action = touched_action >= 0
                                     ? touched_action
                                     : (input.confirm ? model.details_action : -1);
    const int activated_kind = (activated_action >= 0 && activated_action < visible_action_count)
                                   ? action_kinds[static_cast<std::size_t>(activated_action)]
                                   : -1;
    if (activated_kind == kActionPlay) {
        // The cover-download worker does its own SD card I/O (fetching, decoding, writing cache
        // files) on a background thread, completely independent of whatever's about to happen on
        // this one - and BootRom is about to do a lot of its own heavy SD card I/O loading the
        // ROM. Letting both touch the filesystem at once was producing hard-to-reproduce crashes
        // during boot; stopping the worker first (it resumes cleanly from StartCoverDownload next
        // time, already-cached files are never re-fetched) removes that risk entirely rather than
        // relying on the SD card layer to somehow handle true concurrent access safely.
        StopCoverDownload();
        model.pending_rom = entry.path;
        model.pending_program_id = entry.program_id;
        model.app_state = AppState::Booting;
    } else if (activated_kind == kActionSettings) {
        BeginGameOverrides(entry.program_id);
        model.game_settings_program_id = entry.program_id;
        model.game_settings_title = entry.title;
        model.game_settings_before = GetMenuSettings();
        model.game_settings_current = model.game_settings_before;
        model.game_settings_row = 0;
        model.game_settings_row_armed = false;
        model.game_settings_open = true;
    } else if (activated_kind == kActionLayout) {
        LOG_INFO(Frontend, "ui_gamedetails: opening standalone layout editor for program {:016X}",
                entry.program_id);
        BeginGameOverrides(entry.program_id);
        OpenLayoutEditorStandalone();
        if (IsLayoutEditorStandaloneOpen()) {
            model.layout_editor_open = true;
        } else {
            LOG_WARNING(Frontend, "ui_gamedetails: OpenLayoutEditorStandalone failed to open");
            EndGameOverrides();
        }
    } else if (activated_kind == kActionMore) {
        // Keep whatever was logically selected in place across the fold: a recommended item
        // shifts by the header's row-count delta so the same game card stays selected; anything
        // inside the header itself just snaps back to this toggle row, since indices 4-6 only
        // exist while expanded.
        const int old_visible_count = visible_action_count;
        model.details_actions_expanded = !model.details_actions_expanded;
        if (model.details_action >= old_visible_count) {
            const int rec_index = model.details_action - old_visible_count;
            const int new_visible_count = model.details_actions_expanded ? 7 : 4;
            model.details_action = new_visible_count + rec_index;
        } else {
            model.details_action = kActionMore;
        }
    } else if (activated_kind == kActionClearCache) {
        const int cleared = ClearShaderCache(entry.program_id);
        model.notice_text = fmt::format(fmt::runtime(Tr("gamedetails.cache_cleared")), cleared);
        model.notice_is_error = false;
    } else if (activated_kind == kActionCheats) {
        LoadLibraryCheats(entry.program_id);
        model.cheats_program_id = entry.program_id;
        model.cheats_title = entry.title;
        model.cheats_row = 0;
        model.cheats_open = true;
    } else if (activated_kind == kActionMods) {
        LoadTitleMods(entry.program_id);
        model.mods_program_id = entry.program_id;
        model.mods_title = entry.title;
        model.mods_row = 0;
        model.mods_open = true;
    }
    if (touched_action >= 0) {
        model.details_action = touched_action;
    }

    int activated_rec = -1;
    if (touched_rec >= 0) {
        activated_rec = touched_rec;
    } else if (input.confirm && model.details_action >= visible_action_count) {
        activated_rec = model.details_action - visible_action_count;
    }
    // A tap or Confirm on a recommendation jumps straight into its details (same click-to-open
    // pattern as the library list), not a two-step select-then-confirm.
    if (activated_rec >= 0 && static_cast<std::size_t>(activated_rec) < recommended.size()) {
        model.selected_game = static_cast<int>(recommended[static_cast<std::size_t>(activated_rec)]);
        model.details_action = 0;
    }
}

} // namespace SwitchFrontend
