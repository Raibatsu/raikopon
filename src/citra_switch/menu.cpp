// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "citra_switch/canvas.h"
#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/keyboard_prompt.h"
#include "citra_switch/menu.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/rail_icons.h"
#include "citra_switch/settings_model.h"

namespace SwitchFrontend {
namespace {

// Reference alias so every existing g_font.Xxx() call site below keeps working unchanged now
// that Font/Canvas/etc. live in canvas.h/.cpp, shared with the in-game settings screen.
Font& g_font = GetSharedFont();

enum class Tab { Library, Install, Settings, Paths };

// Which pane the cursor lives in.
enum class Focus { Rail, Content };

// Indexed by Tab, so the order has to match the enum.
constexpr std::array<std::pair<Tab, const char*>, 4> kRailItems{{{Tab::Library, "Library"},
                                                                 {Tab::Install, "Install"},
                                                                 {Tab::Settings, "Settings"},
                                                                 {Tab::Paths, "Paths"}}};

constexpr bool RailItemsMatchTabs() {
    for (int i = 0; i < static_cast<int>(kRailItems.size()); ++i) {
        if (static_cast<int>(kRailItems[i].first) != i) {
            return false;
        }
    }
    return true;
}
static_assert(RailItemsMatchTabs(), "kRailItems must be indexable by Tab");

constexpr int kRailFirstY = 96;
constexpr int kRailItemH = 84;
constexpr int kRailItemStep = 108;

int RailItemTop(int index) {
    return kRailFirstY + index * kRailItemStep;
}

std::optional<Tab> RailHitTest(int y) {
    for (int i = 0; i < static_cast<int>(kRailItems.size()); ++i) {
        const int top = RailItemTop(i);
        if (y >= top && y < top + kRailItemH) {
            return kRailItems[i].first;
        }
    }
    return std::nullopt;
}

constexpr int kRailW = 148;
constexpr int kHeaderH = 64;
constexpr int kHintH = 44;
constexpr int kContentX = kRailW;
constexpr int kContentTop = kHeaderH;
constexpr int kContentW = kScreenW - kRailW;
constexpr int kContentBottom = kScreenH - kHintH;

constexpr int kTileW = 196;
constexpr int kTileH = 170;
constexpr int kTileGap = 16;
constexpr int kIconSize = 96;

std::string g_notice;
int g_notice_frames = 0;
bool g_notice_is_error = true;

// ~4 seconds at 60fps.
constexpr int kNoticeFrames = 240;

void ShowNotice(const std::string& text, bool error) {
    g_notice = text;
    g_notice_frames = kNoticeFrames;
    g_notice_is_error = error;
}

// Set for a blocking, single-button popup to show the next time the menu is entered
// (e.g. right after a failed launch attempt). Empty means nothing is queued.
std::string g_pending_popup;

std::string ToLowerAscii(std::string_view s) {
    std::string out{s};
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// Case-insensitive check of `path`'s extension against `ext` (which must include the dot,
// e.g. ".3ds").
bool HasExtension(std::string_view path, std::string_view ext) {
    if (path.size() < ext.size()) {
        return false;
    }
    return ToLowerAscii(path.substr(path.size() - ext.size())) == ToLowerAscii(ext);
}

// Rows on the Paths page.
enum PathRow { PathRowUserDir, PathRowRomsDir, PathRowRecursive, PathRowCount };

constexpr int kPathRowH = 76;
constexpr int kPathToggleH = 52;
constexpr int kPathRowGap = 8;

int PathRowHeight(int row) {
    return row == PathRowRecursive ? kPathToggleH : kPathRowH;
}

int PathRowTop(int row) {
    int y = kContentTop + 16;
    for (int i = 0; i < row; ++i) {
        y += PathRowHeight(i) + kPathRowGap;
    }
    return y;
}

const char* PathRowLabel(int row) {
    switch (row) {
    case PathRowUserDir:
        return "Raikopon Folder";
    case PathRowRomsDir:
        return "ROM Folder";
    default:
        return "Scan Subfolders";
    }
}

// The folder browser covers the whole screen, rail included.
constexpr int kBrowseTop = 108;
constexpr int kBrowseRowH = 44;
constexpr int kBrowseRows = (kContentBottom - kBrowseTop) / kBrowseRowH;

constexpr std::array<std::pair<u64, SwitchFrontend::InputButton>,
                     SwitchFrontend::NumPhysicalButtons>
    kPhysicalButtonMap{{
        {HidNpadButton_A, SwitchFrontend::InputButton::A},
        {HidNpadButton_B, SwitchFrontend::InputButton::B},
        {HidNpadButton_X, SwitchFrontend::InputButton::X},
        {HidNpadButton_Y, SwitchFrontend::InputButton::Y},
        {HidNpadButton_Up, SwitchFrontend::InputButton::Up},
        {HidNpadButton_Down, SwitchFrontend::InputButton::Down},
        {HidNpadButton_Left, SwitchFrontend::InputButton::Left},
        {HidNpadButton_Right, SwitchFrontend::InputButton::Right},
        {HidNpadButton_L, SwitchFrontend::InputButton::L},
        {HidNpadButton_R, SwitchFrontend::InputButton::R},
        {HidNpadButton_Plus, SwitchFrontend::InputButton::Start},
        {HidNpadButton_Minus, SwitchFrontend::InputButton::Select},
        {HidNpadButton_ZL, SwitchFrontend::InputButton::ZL},
        {HidNpadButton_ZR, SwitchFrontend::InputButton::ZR},
        {HidNpadButton_StickL, SwitchFrontend::InputButton::L3},
        {HidNpadButton_StickR, SwitchFrontend::InputButton::R3},
    }};

// Indexed by Tab, so the order has to match the enum.
constexpr std::array<const std::uint8_t*, 4> kRailIconMasks{
    RailIcons::kLibrary, RailIcons::kInstall, RailIcons::kSettings, RailIcons::kPaths};

// Draws a nav-rail icon, tinted like text: the mask supplies coverage only.
void DrawRailIcon(Canvas& canvas, Tab tab, int cx, int cy, u32 color) {
    const std::uint8_t* mask = kRailIconMasks[static_cast<std::size_t>(tab)];
    const int x0 = cx - RailIcons::kSize / 2;
    const int y0 = cy - RailIcons::kSize / 2;
    for (int row = 0; row < RailIcons::kSize; ++row) {
        for (int col = 0; col < RailIcons::kSize; ++col) {
            canvas.Blend(x0 + col, y0 + row, color, mask[row * RailIcons::kSize + col]);
        }
    }
}

void DrawRail(Canvas& canvas, Tab active, Tab cursor, bool rail_focused) {
    canvas.FillRect(0, 0, kRailW, kScreenH, kColRail);
    // The solid accent pill follows the cursor while the rail is focused.
    const Tab pill = rail_focused ? cursor : active;
    for (int i = 0; i < static_cast<int>(kRailItems.size()); ++i) {
        const auto& [tab, label] = kRailItems[i];
        const int y = RailItemTop(i);
        const bool on = tab == pill;
        const bool ghost = rail_focused && tab == active && tab != cursor;
        if (on) {
            canvas.FillRoundRect(12, y, kRailW - 24, kRailItemH, 16, kColAccent);
        } else if (ghost) {
            // Keep a faint marker on the section you came from.
            canvas.FillRoundRect(12, y, kRailW - 24, kRailItemH, 16, kColSurface);
        }
        const u32 fg = on ? kColOnAccent : kColTextDim;
        DrawRailIcon(canvas, tab, kRailW / 2, y + 32, fg);
        const int tw = g_font.Measure(label, 18);
        g_font.Draw(canvas, (kRailW - tw) / 2, y + 68, label, 18, fg);
    }
}

void DrawHeader(Canvas& canvas, std::string_view subtitle) {
    canvas.FillRect(kContentX, 0, kContentW, kHeaderH, kColBg);
    g_font.Draw(canvas, kContentX + 24, CenterBaseline(0, kHeaderH, 28), "Raikopon", 28, kColText);
    if (!subtitle.empty()) {
        const int sw = g_font.Measure(subtitle, 20);
        g_font.Draw(canvas, kScreenW - 24 - sw, CenterBaseline(0, kHeaderH, 20), subtitle, 20,
                    kColTextDim);
    }
    canvas.FillRect(kContentX, kHeaderH - 1, kContentW, 1, kColRail);
}

void DrawNotice(Canvas& canvas) {
    if (g_notice_frames <= 0 || g_notice.empty()) {
        return;
    }
    const int pad = 20;
    const int tw = g_font.Measure(g_notice, 18);
    const int w = tw + pad * 2;
    const int x = kContentX + (kContentW - w) / 2;
    const int y = kContentBottom - 52;
    canvas.FillRoundRect(x, y, w, 36, 10, g_notice_is_error ? kColError : kColAccentDim);
    g_font.Draw(canvas, x + pad, CenterBaseline(y, 36, 18), g_notice, 18, kColText);
}

void DrawTile(Canvas& canvas, const GameEntry& game, int x, int y, bool selected,
              bool content_focused) {
    if (selected && content_focused) {
        canvas.RoundBorder(x, y, kTileW, kTileH, 14, 3, kColAccent, kColSurfaceHi);
    } else if (selected) {
        // Selected but the cursor is on the rail
        canvas.RoundBorder(x, y, kTileW, kTileH, 14, 3, kColBadge, kColSurfaceHi);
    } else {
        canvas.FillRoundRect(x, y, kTileW, kTileH, 14, kColSurface);
    }

    const int icon_x = x + (kTileW - kIconSize) / 2;
    const int icon_y = y + 14;
    if (!game.icon.empty()) {
        canvas.BlitIcon(game.icon, game.icon_size, icon_x, icon_y, kIconSize);
    } else {
        // Placeholder plate with the file type initial.
        canvas.FillRoundRect(icon_x, icon_y, kIconSize, kIconSize, 10, kColBadge);
        const std::string letter = game.file_type.empty() ? "?" : game.file_type.substr(0, 1);
        const int lw = g_font.Measure(letter, 40);
        g_font.Draw(canvas, icon_x + (kIconSize - lw) / 2, CenterBaseline(icon_y, kIconSize, 40),
                    letter, 40, kColTextDim);
    }

    const int text_w = kTileW - 24;
    const std::string title = g_font.Truncate(game.title, 18, text_w);
    const int title_w = g_font.Measure(title, 18);
    g_font.Draw(canvas, x + (kTileW - title_w) / 2, icon_y + kIconSize + 24, title, 18, kColText);

    // File type, then a LOCKED marker for encrypted dumps and an SD marker for installed titles.
    const int badge_y = y + kTileH - 30;
    int badge_x = x + 12;
    if (!game.file_type.empty()) {
        const int bw = g_font.Measure(game.file_type, 14) + 14;
        canvas.FillRoundRect(badge_x, badge_y, bw, 20, 7, kColBadge);
        g_font.Draw(canvas, badge_x + 7, CenterBaseline(badge_y, 20, 14), game.file_type, 14,
                    kColTextDim);
        badge_x += bw + 6;
    }
    if (game.encrypted) {
        const int lw = g_font.Measure("LOCKED", 14) + 14;
        canvas.FillRoundRect(badge_x, badge_y, lw, 20, 7, kColAccentDim);
        g_font.Draw(canvas, badge_x + 7, CenterBaseline(badge_y, 20, 14), "LOCKED", 14, kColText);
        badge_x += lw + 6;
    }
    if (game.installed) {
        const int lw = g_font.Measure("SD", 14) + 14;
        canvas.FillRoundRect(badge_x, badge_y, lw, 20, 7, kColAccentDim);
        g_font.Draw(canvas, badge_x + 7, CenterBaseline(badge_y, 20, 14), "SD", 14, kColText);
    }
}

void DrawEmptyLibrary(Canvas& canvas, const std::string& roms_dir) {
    const char* line1 = "No games found";
    const char* line2 = "Copy .3ds / .cci / .cxi / .3dsx files to";
    const std::string line3 = g_font.TruncateFront(roms_dir, 18, kContentW - 48);
    const char* line4 = "or install a .cia from the Install tab";
    const int cx = kContentX + kContentW / 2;
    const int w1 = g_font.Measure(line1, 26);
    g_font.Draw(canvas, cx - w1 / 2, 300, line1, 26, kColText);
    const int w2 = g_font.Measure(line2, 18);
    g_font.Draw(canvas, cx - w2 / 2, 336, line2, 18, kColTextDim);
    const int w3 = g_font.Measure(line3, 18);
    g_font.Draw(canvas, cx - w3 / 2, 360, line3, 18, kColAccent);
    const int w4 = g_font.Measure(line4, 18);
    g_font.Draw(canvas, cx - w4 / 2, 390, line4, 18, kColTextDim);
}

// Layout of the library grid
struct Grid {
    int cols{};
    int start_x{};
    int top{};
    int visible_rows{};
};

Grid ComputeGrid() {
    Grid g;
    const int avail = kContentW - 48;
    g.cols = std::max(1, (avail + kTileGap) / (kTileW + kTileGap));
    const int used = g.cols * kTileW + (g.cols - 1) * kTileGap;
    g.start_x = kContentX + 24 + (avail - used) / 2;
    g.top = kContentTop + 20;
    g.visible_rows = std::max(1, (kContentBottom - g.top) / (kTileH + kTileGap));
    return g;
}

// General swkbd text prompt, sized for anything from a short name to a multi-line cheat code.
std::string PromptKeyboardText(const std::string& header, const std::string& guide,
                                const std::string& initial, int max_len) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return initial;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header.c_str());
    swkbdConfigSetGuideText(&kbd, guide.c_str());
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(max_len));
    // UTF-8 headroom: worst case is a few bytes per character.
    std::vector<char> out(static_cast<std::size_t>(max_len) * 4 + 16, '\0');
    const Result rc = swkbdShow(&kbd, out.data(), out.size());
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc) ? std::string{out.data()} : initial;
}

// swkbd search prompt
std::string PromptSearch(const std::string& initial) {
    return PromptKeyboardText("Search library", "Game title", initial, 128);
}

std::string FormatSize(u64 bytes) {
    constexpr std::array<const char*, 4> units{"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buf;
}

std::string FormatTitleId(u64 program_id) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(program_id));
    return buf;
}

// Updates and DLC ride on a base title rather than standing alone, so they get the accent.
u32 KindBadgeColor(TitleKind kind) {
    switch (kind) {
    case TitleKind::Update:
    case TitleKind::AddOnContent:
        return kColAccentDim;
    default:
        return kColBadge;
    }
}

// The Install page lists the CIAs in one folder.
constexpr int kInstallHeaderH = 40;
constexpr int kInstallTop = kContentTop + kInstallHeaderH + 8;
constexpr int kInstallRowH = 46;
constexpr int kInstallRows = (kContentBottom - kInstallTop) / kInstallRowH;

// Modal panel listing what is installed alongside one library entry.
void DrawTitleDetails(Canvas& c, const GameEntry& game, const TitleDetails& details) {
    constexpr int w = 660;
    constexpr int h = 356;
    const int x = kContentX + (kContentW - w) / 2;
    const int y = kContentTop + (kContentBottom - kContentTop - h) / 2;
    c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
    c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

    int ty = y + 22;
    g_font.Draw(c, x + 24, ty + 20, g_font.Truncate(game.title, 24, w - 48), 24, kColText);
    ty += 32;
    if (!game.publisher.empty()) {
        g_font.Draw(c, x + 24, ty + 18, g_font.Truncate(game.publisher, 18, w - 48), 18,
                    kColTextDim);
    }
    ty += 30;
    c.FillRect(x + 24, ty, w - 48, 1, kColRail);
    ty += 12;

    const auto row = [&](const char* label, const std::string& value, u32 color) {
        g_font.Draw(c, x + 24, ty + 18, label, 18, kColTextDim);
        g_font.Draw(c, x + 190, ty + 18, g_font.Truncate(value, 18, w - 214), 18, color);
        ty += 30;
    };

    row("Title ID",
        details.program_id == 0 ? std::string{"Unknown"} : FormatTitleId(details.program_id),
        kColText);
    row("Type", TitleKindName(details.kind), kColText);
    row("Source",
        game.installed ? "Installed on SD (" + game.file_type + ")"
                       : "ROM file (" + game.file_type + ")",
        kColText);
    row("Version",
        details.has_base_version ? FormatTitleVersion(details.base_version)
                                 : std::string{"Unknown (no TMD)"},
        details.has_base_version ? kColAccent : kColTextDim);
    row("Update",
        details.has_update ? FormatTitleVersion(details.update_version)
                           : std::string{"Not installed"},
        details.has_update ? kColAccent : kColTextDim);
    row("DLC",
        details.has_dlc ? std::to_string(details.dlc_contents) +
                              (details.dlc_contents == 1 ? " content" : " contents")
                        : std::string{"Not installed"},
        details.has_dlc ? kColAccent : kColTextDim);

    ty += 4;
    g_font.Draw(c, x + 24, ty + 16, g_font.TruncateFront(game.path, 16, w - 48), 16, kColTextDim);

    int hx = x + 24;
    const int hy = y + h - 38;
    hx += DrawHint(c, hx, hy, "+", "Close") + 22;
    if (details.program_id != 0) {
        DrawHint(c, hx, hy, "-", "Clear Shader Cache");
    }
}

class Menu {
public:
    MenuResult Run(PadState& pad) {
        pad_state = &pad;
        // Put a frame up before the ROM scan
        EnsureFramebuffer();
        DrawLoading();
        Present();
        Rescan();
        if (!g_pending_popup.empty()) {
            ShowPopup(g_pending_popup);
            g_pending_popup.clear();
        }
        while (appletMainLoop()) {
            padUpdate(&pad);
            const u64 down = padGetButtonsDown(&pad);
            held = padGetButtons(&pad);

            // +/- together exits the app, but will not allow during an install.
            if (!install_active && (held & (HidNpadButton_Plus | HidNpadButton_Minus)) ==
                                       (HidNpadButton_Plus | HidNpadButton_Minus)) {
                Flush();
                return {MenuAction::Exit, {}};
            }

            const HidAnalogStickState ls = padGetStickPos(&pad, 0);
            constexpr int dz = 12000;
            const u32 nav = repeater.Step(
                (down & HidNpadButton_Up) || ls.y > dz, (down & HidNpadButton_Down) || ls.y < -dz,
                (down & HidNpadButton_Left) || ls.x < -dz,
                (down & HidNpadButton_Right) || ls.x > dz);

            MenuResult result;
            bool done = false;
            if (install_active) {
                PumpInstall();
            } else if (layout_picker_open) {
                HandleLayoutPicker(down, nav);
            } else if (details_open) {
                if ((down & HidNpadButton_Minus) && !(held & HidNpadButton_Plus) &&
                    details.program_id != 0 && !filtered.empty()) {
                    const GameEntry& game = games[filtered[selected]];
                    if (ConfirmClearShaderCache(game)) {
                        const int removed = ClearShaderCache(details.program_id);
                        details_open = false;
                        ShowPopup(removed > 0
                                      ? "Cleared " + std::to_string(removed) +
                                            (removed == 1 ? " file." : " files.")
                                      : "No cached shaders found for this title.",
                                  "Shader Cache", kColAccent);
                    }
                } else if (down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
                    // Any of the buttons that could have opened the panel also closes it.
                    details_open = false;
                }
            } else if (focus == Focus::Rail) {
                HandleRail(down, nav);
            } else if (tab == Tab::Library) {
                done = HandleLibrary(down, nav, result);
            } else if (tab == Tab::Install) {
                HandleInstall(down, nav);
            } else if (tab == Tab::Settings) {
                done = HandleSettings(down, nav);
            } else {
                done = HandlePaths(down, nav);
            }
            if (done) {
                return result;
            }

            if (!install_active && !details_open && !layout_picker_open) {
                HandleTouch();
            }
            if (pending_launch) {
                MenuResult launch{MenuAction::Launch, *pending_launch};
                pending_launch.reset();
                return launch;
            }

            Draw();
            Present();
            if (g_notice_frames > 0) {
                --g_notice_frames;
            }
        }
        Flush();
        return {MenuAction::Exit, {}};
    }

    // Releasing the framebuffer hands the nwindow back so the emulator's renderer can claim it for the launched game.
    ~Menu() {
        // Only reachable with a worker still running if appletMainLoop() bowed out mid-install.
        if (install_thread.joinable()) {
            install_thread.join();
        }
        if (fb_ready) {
            framebufferClose(&fb);
        }
    }

private:
    Tab tab{Tab::Library};
    Focus focus{Focus::Content};
    Tab rail_sel{Tab::Library}; // Highlighted rail item while focused.
    std::vector<GameEntry> games;
    std::vector<int> filtered; // Indices into `games` after search filtering.
    int selected = 0;          // Index into `filtered`.
    int scroll_row = 0;
    int settings_sel = 0;
    int settings_scroll = 0;
    bool settings_armed = false;
    SettingsTab settings_tab = SettingsTab::Graphics;
    bool gyro_edit_y = false;
    int paths_sel = 0;
    std::string search;
    MenuSettings settings{};
    SwitchPaths paths{};
    Repeater repeater;
    Framebuffer fb{};
    PadState* pad_state = nullptr;
    u64 held = 0; // This frame's held buttons.
    bool fb_ready = false;
    bool settings_dirty = false; // Edited settings not yet written to config.ini.
    bool paths_dirty = false;
    bool settings_reset_confirm = false; // "Reset to defaults?" popup showing on the Settings tab.

    int remap_sel = 0;
    int remap_scroll = 0;
    bool remap_capturing = false;
    bool remap_dirty = false;

    // Library detail panel.
    bool details_open = false;
    TitleDetails details{};

    // R3 screen-layout picker.
    bool layout_picker_open = false;
    int layout_picker_sel = 0;

    // Install page.
    std::string install_dir;
    std::vector<DirEntry> install_dirs;
    std::vector<CiaEntry> install_cias;
    int install_sel = 0;
    int install_scroll = 0;
    bool install_listed = false; // Whether install_dir has been read at least once.

    // The installation worker.
    std::thread install_thread;
    std::atomic<bool> install_done{false};
    std::atomic<std::size_t> install_written{0};
    std::atomic<std::size_t> install_total{0};
    InstallResult install_result{};
    std::string install_name;
    bool install_active = false;

    void Rescan() {
        games = ScanGames();
        settings = GetMenuSettings();
        paths = GetPaths();
        if (install_dir.empty()) {
            install_dir = paths.roms_dir;
        }
        ApplyFilter();
    }

    // Switch tabs persisting any pending edits when leaving an editing page so a disk write
    // happens once per editing session rather than once per adjustment.
    void SetTab(Tab next) {
        if (tab == Tab::Settings && next != Tab::Settings) {
            FlushSettings();
            FlushRemap();
            settings_armed = false;
        }
        if (tab == Tab::Paths && next != Tab::Paths) {
            // The library on screen came from the old directory so it must be re-read.
            const bool stale = ScanInputsChanged();
            FlushPaths();
            if (stale) {
                ShowBusy("Refreshing library...");
                search.clear();
                Rescan();
            }
        }
        tab = next;
        if (tab == Tab::Install && !install_listed) {
            ShowBusy("Reading CIAs...");
            RefreshInstallList();
        }
    }

    // True while the edited scan inputs differ from what the last scan used.
    bool ScanInputsChanged() const {
        const SwitchPaths& live = GetPaths();
        return paths.roms_dir != live.roms_dir || paths.scan_recursive != live.scan_recursive;
    }

    // The dekopon directory only moves on the next launch.
    bool RestartPending() const {
        return paths.user_dir != GetActiveUserDir();
    }

    void Flush() {
        FlushSettings();
        FlushRemap();
        FlushPaths();
    }

    void FlushSettings() {
        if (settings_dirty) {
            SetMenuSettings(settings);
            settings_dirty = false;
        }
    }

    void FlushRemap() {
        if (remap_dirty) {
            SwitchFrontend::ApplyButtonMappings();
            SwitchFrontend::SaveConfig();
            remap_dirty = false;
        }
    }

    void SwitchSettingsTab(int delta) {
        FlushRemap();
        const int idx = (static_cast<int>(settings_tab) + delta + kNumSettingsTabs) %
                        kNumSettingsTabs;
        settings_tab = static_cast<SettingsTab>(idx);
        settings_sel = 0;
        settings_scroll = 0;
        settings_armed = false;
        gyro_edit_y = false;
        remap_sel = 0;
        remap_scroll = 0;
        remap_capturing = false;
    }

    void FlushPaths() {
        if (paths_dirty) {
            SetPaths(paths);
            paths_dirty = false;
        }
    }

    void ApplyFilter() {
        filtered.clear();
        const std::string needle = ToLowerAscii(search);
        for (int i = 0; i < static_cast<int>(games.size()); ++i) {
            if (needle.empty() || ToLowerAscii(games[i].title).find(needle) != std::string::npos) {
                filtered.push_back(i);
            }
        }
        selected = std::clamp(selected, 0, std::max(0, static_cast<int>(filtered.size()) - 1));
        scroll_row = 0;
    }

    // Returns true if the menu should return `result` to the caller.
    bool HandleLibrary(u64 down, u32 nav, MenuResult& result) {
        const Grid grid = ComputeGrid();
        const int count = static_cast<int>(filtered.size());
        if (count > 0) {
            if (nav & DirLeft) {
                selected = std::max(0, selected - 1);
            }
            if (nav & DirRight) {
                selected = std::min(count - 1, selected + 1);
            }
            if (nav & DirUp) {
                selected = std::max(0, selected - grid.cols);
            }
            if (nav & DirDown) {
                selected = std::min(count - 1, selected + grid.cols);
            }
            if (down & HidNpadButton_A) {
                const std::string& path = games[filtered[selected]].path;
                if (!HasExtension(path, ".3ds") || ConfirmDecryptWarning()) {
                    result = {MenuAction::Launch, path};
                    return true;
                }
            }
            // Guarded so that reaching for the +/- exit combo doesn't flash the panel open.
            if ((down & HidNpadButton_Plus) && !(held & HidNpadButton_Minus)) {
                details = GetTitleDetails(games[filtered[selected]]);
                details_open = true;
            }
        }
        if (down & HidNpadButton_X) {
            search = PromptSearch(search);
            ApplyFilter();
        }
        if (down & HidNpadButton_Y) {
            // Rescanning the SD card blocks
            ShowBusy("Refreshing library…");
            search.clear();
            Rescan();
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
        EnsureVisible(grid);
        return false;
    }

    // Row model for the Install page: ".." (unless at a device root), then subfolders, then CIAs.
    int InstallParentRows() const {
        return ParentDirectory(install_dir).empty() ? 0 : 1;
    }

    int InstallRowCount() const {
        return InstallParentRows() + static_cast<int>(install_dirs.size()) +
               static_cast<int>(install_cias.size());
    }

    // The CIA under the cursor, or nothing when it sits on ".." or a folder.
    const CiaEntry* SelectedCia() const {
        const int i = install_sel - InstallParentRows() - static_cast<int>(install_dirs.size());
        if (i < 0 || i >= static_cast<int>(install_cias.size())) {
            return nullptr;
        }
        return &install_cias[i];
    }

    void RefreshInstallList() {
        install_dirs = ListSubdirectories(install_dir);
        install_cias = ListCiaFiles(install_dir);
        install_sel = std::clamp(install_sel, 0, std::max(0, InstallRowCount() - 1));
        install_listed = true;
    }

    void EnterInstallDir(const std::string& next) {
        install_dir = next;
        install_sel = 0;
        install_scroll = 0;
        RefreshInstallList();
    }

    void HandleInstall(u64 down, u32 nav) {
        const int count = InstallRowCount();
        install_sel = std::clamp(install_sel, 0, std::max(0, count - 1));
        if (nav & DirUp) {
            install_sel = std::max(0, install_sel - 1);
        }
        if (nav & DirDown) {
            install_sel = std::min(std::max(0, count - 1), install_sel + 1);
        }
        install_scroll = std::clamp(install_scroll, std::max(0, install_sel - kInstallRows + 1),
                                    std::max(0, std::min(install_sel, count - kInstallRows)));
        if (down & HidNpadButton_A) {
            const int base = InstallParentRows();
            const int di = install_sel - base;
            if (base == 1 && install_sel == 0) {
                EnterInstallDir(ParentDirectory(install_dir));
            } else if (di < static_cast<int>(install_dirs.size())) {
                EnterInstallDir(install_dirs[di].path);
            } else if (const CiaEntry* cia = SelectedCia()) {
                TryStartInstall(*cia);
            }
        }
        if (down & HidNpadButton_Y) {
            ShowBusy("Reading CIAs...");
            RefreshInstallList();
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
    }

    void TryStartInstall(const CiaEntry& cia) {
        if (!cia.readable) {
            ShowNotice(cia.name + ": not a valid CIA", true);
            return;
        }
        if (ConfirmInstall(cia)) {
            StartInstall(cia);
        }
    }

    void StartInstall(const CiaEntry& cia) {
        install_name = cia.name;
        install_written = 0;
        install_total = cia.size;
        install_done = false;
        install_active = true;
        install_thread = std::thread([this, path = cia.path] {
            // InstallCia() itself holds the CPU boost for the duration of the write.
            install_result = InstallCia(path, [this](std::size_t written, std::size_t total) {
                install_written = written;
                install_total = total;
            });
            install_done = true;
        });
    }

    void PumpInstall() {
        if (!install_done) {
            return;
        }
        install_thread.join();
        install_active = false;
        const bool ok = install_result == InstallResult::Success;
        ShowNotice(install_name + ": " + InstallResultText(install_result), !ok);
        RefreshInstallList();
        if (ok) {
            // A successful install may have put a new title in the library's reach.
            games = ScanGames();
            ApplyFilter();
        }
    }

    // Blocks on a yes/no prompt.
    bool ConfirmInstall(const CiaEntry& cia) {
        u16 installed_version = 0;
        const bool replacing = GetInstalledVersion(cia.program_id, installed_version);
        while (appletMainLoop()) {
            padUpdate(pad_state);
            const u64 down = padGetButtonsDown(pad_state);
            if (down & HidNpadButton_A) {
                return true;
            }
            if (down & HidNpadButton_B) {
                return false;
            }
            Draw();
            DrawConfirmInstall(canvas, cia, replacing, installed_version);
            Present();
        }
        return false;
    }

    // Blocks on a warning shown before launching a raw .3ds dump, which is usually still
    // encrypted and won't boot. Returns true if the user chooses to launch anyway.
    bool ConfirmDecryptWarning() {
        while (appletMainLoop()) {
            padUpdate(pad_state);
            const u64 down = padGetButtonsDown(pad_state);
            if (down & HidNpadButton_A) {
                return true;
            }
            if (down & HidNpadButton_B) {
                return false;
            }
            Draw();
            DrawDecryptWarning(canvas);
            Present();
        }
        return false;
    }

    bool ConfirmClearShaderCache(const GameEntry& game) {
        while (appletMainLoop()) {
            padUpdate(pad_state);
            const u64 down = padGetButtonsDown(pad_state);
            if (down & HidNpadButton_A) {
                return true;
            }
            if (down & HidNpadButton_B) {
                return false;
            }
            Draw();
            DrawConfirmClearShaderCache(canvas, game);
            Present();
        }
        return false;
    }

    // Blocks on a single-button acknowledgement popup, e.g. after a failed launch.
    void ShowPopup(const std::string& message, const char* title = "Can't launch",
                   u32 title_color = kColError) {
        while (appletMainLoop()) {
            padUpdate(pad_state);
            const u64 down = padGetButtonsDown(pad_state);
            if (down & (HidNpadButton_A | HidNpadButton_B)) {
                return;
            }
            Draw();
            DrawPopup(canvas, message, title, title_color);
            Present();
        }
    }

    // Move the cursor out to the Library/Settings rail
    void EnterRail() {
        focus = Focus::Rail;
        rail_sel = tab;
    }

    void HandleRail(u64 down, u32 nav) {
        int index = static_cast<int>(rail_sel);
        if (nav & DirUp) {
            index = std::max(0, index - 1);
        }
        if (nav & DirDown) {
            index = std::min(static_cast<int>(kRailItems.size()) - 1, index + 1);
        }
        rail_sel = kRailItems[index].first;
        if (down & HidNpadButton_A) {
            SetTab(rail_sel);
            focus = Focus::Content;
        }
        // B cancels back into the section left.
        if (down & HidNpadButton_B) {
            rail_sel = tab;
            focus = Focus::Content;
        }
    }

    void OpenLayoutPicker() {
        layout_picker_sel = 0;
        layout_picker_open = true;
    }

    bool HandleSettings(u64 down, u32 nav) {
        if (settings_tab == SettingsTab::Controls) {
            return HandleControlsTab(down, nav);
        }

        if (settings_reset_confirm) {
            if (down & HidNpadButton_A) {
                settings = DefaultMenuSettings();
                SetMenuSettings(settings);
                settings_dirty = false;
                settings_reset_confirm = false;
            } else if (down & HidNpadButton_B) {
                settings_reset_confirm = false;
            }
            return false;
        }
        if (down & HidNpadButton_Minus) {
            settings_reset_confirm = true;
            return false;
        }

        if (down & HidNpadButton_L) {
            SwitchSettingsTab(-1);
            return false;
        }
        if (down & HidNpadButton_R) {
            SwitchSettingsTab(+1);
            return false;
        }

        const auto rows = BuildSettingRows(settings_tab, settings);
        const int count = static_cast<int>(rows.size());

        if (settings_armed) {
            const SettingRowIdx current_item = rows[settings_sel].item;
            bool changed = false;
            if (current_item == SettingRowGyroSensitivity) {
                if (nav & DirUp) {
                    gyro_edit_y = false;
                }
                if (nav & DirDown) {
                    gyro_edit_y = true;
                }
                if (nav & DirLeft) {
                    AdjustGyroAxis(settings, gyro_edit_y, -1);
                    changed = true;
                }
                if (nav & DirRight) {
                    AdjustGyroAxis(settings, gyro_edit_y, +1);
                    changed = true;
                }
            } else {
                if (nav & DirLeft) {
                    CycleSetting(settings, current_item, -1);
                    changed = true;
                }
                if (nav & DirRight) {
                    CycleSetting(settings, current_item, +1);
                    changed = true;
                }
            }
            if (changed) {
                settings_dirty = true;
            }
            if (down & (HidNpadButton_A | HidNpadButton_B)) {
                settings_armed = false;
            }
            return false;
        }

        if (nav & DirUp) {
            settings_sel = std::max(0, settings_sel - 1);
        }
        if (nav & DirDown) {
            settings_sel = std::min(count - 1, settings_sel + 1);
        }
        settings_scroll =
            std::clamp(settings_scroll, std::max(0, settings_sel - kSettingsRows + 1),
                       std::max(0, std::min(settings_sel, count - kSettingsRows)));

        const SettingRowIdx current_item = rows[settings_sel].item;
        if (current_item == SettingRowLayoutCycle) {
            if ((down & HidNpadButton_A) || (nav & DirRight)) {
                OpenLayoutPicker();
            }
            if (down & HidNpadButton_B) {
                EnterRail();
            }
            return false;
        }
        // Edit the local snapshot only and use FlushSettings() later to apply to config.ini.
        if (down & HidNpadButton_A) {
            if (IsBooleanSetting(current_item)) {
                ToggleSetting(settings, current_item);
                settings_dirty = true;
            } else {
                settings_armed = true;
                gyro_edit_y = false;
            }
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
        return false;
    }

    bool HandleControlsTab(u64 down, u32 nav) {
        if (remap_capturing) {
            for (const auto& [hid_mask, phys] : kPhysicalButtonMap) {
                if ((down & hid_mask) != 0) {
                    SwitchFrontend::SetMapping(
                        static_cast<SwitchFrontend::MappableControl>(remap_sel), phys);
                    remap_capturing = false;
                    remap_dirty = true;
                    break;
                }
            }
            return false;
        }

        if (down & HidNpadButton_L) {
            SwitchSettingsTab(-1);
            return false;
        }
        if (down & HidNpadButton_R) {
            SwitchSettingsTab(+1);
            return false;
        }

        const int count = SwitchFrontend::NumMappableControls;
        if (nav & DirUp) {
            remap_sel = std::max(0, remap_sel - 1);
        }
        if (nav & DirDown) {
            remap_sel = std::min(count - 1, remap_sel + 1);
        }
        remap_scroll = std::clamp(remap_scroll, std::max(0, remap_sel - kSettingsRows + 1),
                                  std::max(0, std::min(remap_sel, count - kSettingsRows)));
        if (down & HidNpadButton_A) {
            remap_capturing = true;
        }
        if (down & HidNpadButton_Y) {
            const auto control = static_cast<SwitchFrontend::MappableControl>(remap_sel);
            SwitchFrontend::SetMapping(control, SwitchFrontend::DefaultMapping(control));
            remap_dirty = true;
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
        return false;
    }

    void HandleLayoutPicker(u64 down, u32 nav) {
        const int count = GetScreenLayoutCount();
        if (nav & DirUp) {
            layout_picker_sel = (layout_picker_sel - 1 + count) % count;
        }
        if (nav & DirDown) {
            layout_picker_sel = (layout_picker_sel + 1) % count;
        }
        if (down & HidNpadButton_A) {
            settings.layout_cycle_mask ^= (1u << layout_picker_sel);
            settings_dirty = true;
        }
        if (down & HidNpadButton_B) {
            layout_picker_open = false;
        }
    }

    bool HandlePaths(u64 down, u32 nav) {
        if (nav & DirUp) {
            paths_sel = std::max(0, paths_sel - 1);
        }
        if (nav & DirDown) {
            paths_sel = std::min(PathRowCount - 1, paths_sel + 1);
        }
        if (paths_sel == PathRowRecursive) {
            if (nav & DirLeft) {
                paths.scan_recursive = false;
                paths_dirty = true;
            }
            if (nav & DirRight) {
                paths.scan_recursive = true;
                paths_dirty = true;
            }
            if (down & HidNpadButton_A) {
                paths.scan_recursive = !paths.scan_recursive;
                paths_dirty = true;
            }
        } else if (down & HidNpadButton_A) {
            PickFolder(paths_sel);
        }
        if (down & HidNpadButton_Y) {
            ResetToDefault(paths_sel);
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
        return false;
    }

    void PickFolder(int row) {
        const std::string& current = row == PathRowUserDir ? paths.user_dir : paths.roms_dir;
        const std::optional<std::string> picked = BrowseForFolder(current);
        if (!picked) {
            return;
        }
        if (row == PathRowUserDir) {
            paths.user_dir = *picked;
        } else {
            paths.roms_dir = *picked;
        }
        paths_dirty = true;
    }

    void ResetToDefault(int row) {
        switch (row) {
        case PathRowUserDir:
            paths.user_dir = GetDefaultUserDir();
            break;
        case PathRowRomsDir:
            paths.roms_dir = GetDefaultRomsDir(paths.user_dir);
            break;
        default:
            paths.scan_recursive = true;
            break;
        }
        paths_dirty = true;
    }

    void EnsureVisible(const Grid& grid) {
        if (filtered.empty()) {
            return;
        }
        const int row = selected / grid.cols;
        if (row < scroll_row) {
            scroll_row = row;
        } else if (row >= scroll_row + grid.visible_rows) {
            scroll_row = row - grid.visible_rows + 1;
        }
    }

    void HandleTouch() {
        HidTouchScreenState ts{};
        if (hidGetTouchScreenStates(&ts, 1) == 0 || ts.count == 0) {
            if (touch_was_down && touch_swipe_candidate) {
                const int dx = touch_last_x - touch_start_x;
                const int dy = touch_last_y - touch_start_y;
                constexpr int kSwipeThreshold = 120;
                constexpr int kSwipeMaxVertical = 80;
                if (std::abs(dx) >= kSwipeThreshold && std::abs(dy) <= kSwipeMaxVertical) {
                    SwitchSettingsTab(dx < 0 ? +1 : -1);
                }
            }
            touch_was_down = false;
            touch_swipe_candidate = false;
            return;
        }
        const int tx = static_cast<int>(ts.touches[0].x);
        const int ty = static_cast<int>(ts.touches[0].y);
        touch_last_x = tx;
        touch_last_y = ty;
        if (touch_was_down) {
            return;
        }
        touch_was_down = true;
        touch_start_x = tx;
        touch_start_y = ty;
        touch_swipe_candidate =
            tab == Tab::Settings && tx >= kRailW && !settings_armed && !remap_capturing;

        if (tx < kRailW) {
            if (const std::optional<Tab> hit = RailHitTest(ty)) {
                SetTab(*hit);
                rail_sel = tab;
                focus = Focus::Content;
            }
            return;
        }
        if (tab == Tab::Library) {
            const Grid grid = ComputeGrid();
            for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
                int tile_x, tile_y;
                if (!TileRect(grid, i, tile_x, tile_y)) {
                    continue;
                }
                if (tx >= tile_x && tx < tile_x + kTileW && ty >= tile_y &&
                    ty < tile_y + kTileH) {
                    if (selected == i) {
                        // Second tap launches.
                        const std::string& path = games[filtered[i]].path;
                        if (!HasExtension(path, ".3ds") || ConfirmDecryptWarning()) {
                            pending_launch = path;
                        }
                    }
                    selected = i;
                    EnsureVisible(grid);
                    break;
                }
            }
        } else if (tab == Tab::Install) {
            const int row = install_scroll + (ty - kInstallTop) / kInstallRowH;
            if (ty >= kInstallTop && ty < kContentBottom && row < InstallRowCount()) {
                install_sel = row;
            }
        } else if (tab == Tab::Settings && ty >= kContentTop && ty < kContentTop + kTabBarH) {
            if (!(settings_tab == SettingsTab::Controls && remap_capturing)) {
                SwitchSettingsTab(tx < kContentX + kContentW / 2 ? -1 : +1);
            }
        } else if (tab == Tab::Settings && settings_tab == SettingsTab::Controls) {
            const int row = remap_scroll + (ty - kSettingsTop) / (kRowH + 8);
            const int rows_bottom = kSettingsTop + kSettingsRows * (kRowH + 8);
            const int count = SwitchFrontend::NumMappableControls;
            if (!remap_capturing && ty >= kSettingsTop && ty < rows_bottom && row < count) {
                if (row != remap_sel) {
                    remap_sel = row;
                } else {
                    remap_capturing = true;
                }
            }
        } else if (tab == Tab::Settings) {
            const auto rows = BuildSettingRows(settings_tab, settings);
            const int row = settings_scroll + (ty - kSettingsTop) / (kRowH + 8);
            const int rows_bottom = kSettingsTop + kSettingsRows * (kRowH + 8);
            if (ty >= kSettingsTop && ty < rows_bottom && row < static_cast<int>(rows.size())) {
                const SettingRowIdx current_item = rows[row].item;
                if (row != settings_sel) {
                    // First tap on a row just selects it, mirroring joystick navigation — same
                    // "one tap to select, another to modify" rule as the gamepad path below.
                    settings_sel = row;
                    settings_armed = false;
                } else if (current_item == SettingRowLayoutCycle) {
                    OpenLayoutPicker();
                } else if (settings_armed) {
                    // Already armed: tapping either half adjusts it, same as joystick left/right.
                    const int dir = tx > kContentX + kContentW / 2 ? +1 : -1;
                    if (current_item == SettingRowGyroSensitivity) {
                        AdjustGyroAxis(settings, gyro_edit_y, dir);
                    } else {
                        CycleSetting(settings, current_item, dir);
                    }
                    settings_dirty = true;
                } else if (IsBooleanSetting(current_item)) {
                    ToggleSetting(settings, current_item);
                    settings_dirty = true;
                } else {
                    settings_armed = true;
                    gyro_edit_y = false;
                }
            }
        } else {
            for (int i = 0; i < PathRowCount; ++i) {
                const int y = PathRowTop(i);
                if (ty < y || ty >= y + PathRowHeight(i)) {
                    continue;
                }
                paths_sel = i;
                if (i == PathRowRecursive) {
                    paths.scan_recursive = !paths.scan_recursive;
                    paths_dirty = true;
                } else {
                    PickFolder(i);
                }
                break;
            }
        }
    }

    // Screen rect of filtered tile `i` given the current scroll.
    bool TileRect(const Grid& grid, int i, int& out_x, int& out_y) {
        const int row = i / grid.cols;
        const int col = i % grid.cols;
        const int y = grid.top + (row - scroll_row) * (kTileH + kTileGap);
        if (row < scroll_row || row >= scroll_row + grid.visible_rows) {
            return false;
        }
        out_x = grid.start_x + col * (kTileW + kTileGap);
        out_y = y;
        return true;
    }

    // Modal folder picker. Blocks until a folder is chosen or the user backs out.
    std::optional<std::string> BrowseForFolder(const std::string& start) {
        std::string dir = EnsureDirectory(start) ? start : std::string{"sdmc:/"};
        std::vector<DirEntry> entries = ListSubdirectories(dir);
        int sel = 0;
        int scroll = 0;
        Repeater rep;

        auto Enter = [&](const std::string& next, const std::string& highlight) {
            dir = next;
            entries = ListSubdirectories(dir);
            const int base = ParentDirectory(dir).empty() ? 0 : 1;
            sel = 0;
            scroll = 0;
            for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                if (entries[i].path == highlight) {
                    sel = i + base;
                    break;
                }
            }
        };

        while (appletMainLoop()) {
            padUpdate(pad_state);
            const u64 down = padGetButtonsDown(pad_state);
            const HidAnalogStickState ls = padGetStickPos(pad_state, 0);
            constexpr int dz = 12000;
            const u32 nav = rep.Step((down & HidNpadButton_Up) || ls.y > dz,
                                     (down & HidNpadButton_Down) || ls.y < -dz, false, false);

            const std::string parent = ParentDirectory(dir);
            // Row 0 is ".." everywhere but device root.
            const int base = parent.empty() ? 0 : 1;
            const int count = static_cast<int>(entries.size()) + base;
            sel = std::clamp(sel, 0, std::max(0, count - 1));

            if (nav & DirUp) {
                sel = std::max(0, sel - 1);
            }
            if (nav & DirDown) {
                sel = std::min(std::max(0, count - 1), sel + 1);
            }
            scroll = std::clamp(scroll, std::max(0, sel - kBrowseRows + 1),
                                std::max(0, std::min(sel, count - kBrowseRows)));
            if (down & HidNpadButton_A) {
                if (base == 1 && sel == 0) {
                    Enter(parent, dir);
                } else if (sel >= base && sel - base < static_cast<int>(entries.size())) {
                    Enter(entries[sel - base].path, "");
                }
            }
            if (down & HidNpadButton_B) {
                if (parent.empty()) {
                    return std::nullopt;
                }
                Enter(parent, dir);
            }
            if (down & HidNpadButton_Y) {
                return std::nullopt;
            }
            if (down & HidNpadButton_Plus) {
                return dir;
            }

            DrawBrowser(dir, entries, sel, scroll);
            Present();
        }
        return std::nullopt;
    }

    void DrawBrowser(const std::string& dir, const std::vector<DirEntry>& entries, int sel,
                     int scroll) {
        Canvas& c = canvas;
        c.Clear(kColBg);

        g_font.Draw(c, 40, 44, "Select folder", 28, kColText);
        g_font.Draw(c, 40, 76, g_font.TruncateFront(dir, 20, kScreenW - 80), 20, kColAccent);
        c.FillRect(40, 96, kScreenW - 80, 1, kColRail);

        const bool has_parent = !ParentDirectory(dir).empty();
        const int base = has_parent ? 1 : 0;
        const int count = static_cast<int>(entries.size()) + base;

        if (count == 0) {
            g_font.Draw(c, 52, kBrowseTop + 30, "No subfolders here", 20, kColTextDim);
        }
        for (int i = scroll; i < std::min(count, scroll + kBrowseRows); ++i) {
            const int y = kBrowseTop + (i - scroll) * kBrowseRowH;
            if (i == sel) {
                c.FillRoundRect(32, y, kScreenW - 64, kBrowseRowH - 4, 8, kColSurfaceHi);
                c.FillRoundRect(32, y + 8, 4, kBrowseRowH - 20, 2, kColAccent);
            }
            const bool up = has_parent && i == 0;
            const std::string name = up ? ".." : entries[i - base].name + "/";
            g_font.Draw(c, 52, CenterBaseline(y, kBrowseRowH - 4, 20),
                        g_font.Truncate(name, 20, kScreenW - 128), 20, up ? kColTextDim : kColText);
        }
        DrawListScrollbar(c, kScreenW - 20, kBrowseTop, kBrowseRows, kBrowseRowH, count, scroll);

        c.FillRect(0, kContentBottom, kScreenW, kHintH, kColHintBar);
        c.FillRect(0, kContentBottom, kScreenW, 1, kColRail);
        int hx = 40;
        const int hy = kContentBottom + (kHintH - 26) / 2;
        hx += DrawHint(c, hx, hy, "A", "Open") + 22;
        hx += DrawHint(c, hx, hy, "B", has_parent ? "Up" : "Cancel") + 22;
        hx += DrawHint(c, hx, hy, "+", "Select this folder") + 22;
        DrawHint(c, hx, hy, "Y", "Cancel");
    }

    void DrawControlsTab(Canvas& c, bool content_focus) {
        const int x = kContentX + 24;
        const int w = kContentW - 48;
        const int count = SwitchFrontend::NumMappableControls;
        for (int i = remap_scroll; i < std::min(count, remap_scroll + kSettingsRows); ++i) {
            const auto control = static_cast<SwitchFrontend::MappableControl>(i);
            const int y = kSettingsTop + (i - remap_scroll) * (kRowH + 8);
            const bool on = i == remap_sel;
            if (on) {
                c.FillRoundRect(x, y, w, kRowH, 10, content_focus ? kColSurfaceHi : kColSurface);
                c.FillRoundRect(x, y + 8, 4, kRowH - 16, 2,
                                content_focus ? kColAccent : kColBadge);
            }
            g_font.Draw(c, x + 20, CenterBaseline(y, kRowH, 22), SwitchFrontend::ControlName(control),
                        22, kColText);
            const std::string value =
                SwitchFrontend::PhysicalButtonName(SwitchFrontend::GetMapping(control));
            const int vw = g_font.Measure(value, 22);
            g_font.Draw(c, x + w - 24 - vw, CenterBaseline(y, kRowH, 22), value, 22,
                        on && content_focus ? kColAccent : kColTextDim);
        }
        DrawListScrollbar(c, kScreenW - 10, kSettingsTop, kSettingsRows, kRowH + 8, count,
                          remap_scroll);
    }

    void DrawPathsPage(Canvas& c) {
        DrawHeader(c, "");
        const bool content_focus = focus == Focus::Content;
        const int x = kContentX + 24;
        const int w = kContentW - 48;
        for (int i = 0; i < PathRowCount; ++i) {
            const int y = PathRowTop(i);
            const int h = PathRowHeight(i);
            const bool on = i == paths_sel;
            if (on) {
                c.FillRoundRect(x, y, w, h, 10, content_focus ? kColSurfaceHi : kColSurface);
                c.FillRoundRect(x, y + 8, 4, h - 16, 2, content_focus ? kColAccent : kColBadge);
            }
            if (i == PathRowRecursive) {
                g_font.Draw(c, x + 20, CenterBaseline(y, h, 22), PathRowLabel(i), 22, kColText);
                const char* value = paths.scan_recursive ? "On" : "Off";
                const int vw = g_font.Measure(value, 22);
                g_font.Draw(c, x + w - 24 - vw, CenterBaseline(y, h, 22), value, 22,
                            on && content_focus ? kColAccent : kColTextDim);
                continue;
            }
            g_font.Draw(c, x + 20, y + 30, PathRowLabel(i), 22, kColText);
            const std::string& dir = i == PathRowUserDir ? paths.user_dir : paths.roms_dir;
            g_font.Draw(c, x + 20, y + 58, g_font.TruncateFront(dir, 18, w - 44), 18,
                        on && content_focus ? kColAccent : kColTextDim);
        }

        int y = PathRowTop(PathRowCount - 1) + PathRowHeight(PathRowCount - 1) + 30;
        if (RestartPending()) {
            g_font.Draw(c, x + 20, y, "Restart Raikopon to move to the new folder.", 18, kColAccent);
            y += 26;
        }

        if (focus == Focus::Rail) {
            DrawRailHints(c);
        } else {
            int hx = kContentX + 24;
            const int hy = kContentBottom + (kHintH - 26) / 2;
            hx +=
                DrawHint(c, hx, hy, "A", paths_sel == PathRowRecursive ? "Toggle" : "Browse") + 22;
            hx += DrawHint(c, hx, hy, "Y", "Default") + 22;
            hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
            DrawHint(c, hx, hy, "+ -", "Exit");
        }
    }

    void Draw() {
        Canvas& c = canvas;
        c.Clear(kColBg);
        DrawRail(c, tab, rail_sel, focus == Focus::Rail);
        if (tab == Tab::Library) {
            DrawLibrary(c);
        } else if (tab == Tab::Install) {
            DrawInstallPage(c);
        } else if (tab == Tab::Settings) {
            DrawSettingsPage(c);
        } else {
            DrawPathsPage(c);
        }
        DrawNotice(c);
        DrawHintBar(c);
        if (details_open && !filtered.empty()) {
            DrawTitleDetails(c, games[filtered[selected]], details);
        }
        if (layout_picker_open) {
            DrawLayoutPicker(c);
        }
        if (install_active) {
            DrawInstallProgress(c);
        }
    }

    void DrawInstallPage(Canvas& c) {
        const std::size_t count_cias = install_cias.size();
        DrawHeader(c, std::to_string(count_cias) + (count_cias == 1 ? " CIA" : " CIAs"));
        const bool content_focus = focus == Focus::Content;
        const int x = kContentX + 24;
        const int w = kContentW - 48;
        g_font.Draw(c, x, kContentTop + 26, g_font.TruncateFront(install_dir, 18, w), 18,
                    kColAccent);
        c.FillRect(x, kContentTop + kInstallHeaderH, w, 1, kColRail);

        const int count = InstallRowCount();
        if (count == 0) {
            g_font.Draw(c, x + 20, kInstallTop + 30, "No CIAs or subfolders here", 20, kColTextDim);
        }
        const int base = InstallParentRows();
        for (int i = install_scroll; i < std::min(count, install_scroll + kInstallRows); ++i) {
            const int y = kInstallTop + (i - install_scroll) * kInstallRowH;
            if (i == install_sel) {
                c.FillRoundRect(x, y, w, kInstallRowH - 4, 8,
                                content_focus ? kColSurfaceHi : kColSurface);
                c.FillRoundRect(x, y + 8, 4, kInstallRowH - 20, 2,
                                content_focus ? kColAccent : kColBadge);
            }
            const int text_y = CenterBaseline(y, kInstallRowH - 4, 18);
            if (base == 1 && i == 0) {
                g_font.Draw(c, x + 20, text_y, "..", 18, kColTextDim);
                continue;
            }
            const int di = i - base;
            if (di < static_cast<int>(install_dirs.size())) {
                g_font.Draw(c, x + 20, text_y,
                            g_font.Truncate(install_dirs[di].name + "/", 18, w - 44), 18, kColText);
                continue;
            }
            DrawCiaRow(c, install_cias[di - static_cast<int>(install_dirs.size())], x, y, w);
        }
        DrawListScrollbar(c, kScreenW - 10, kInstallTop, kInstallRows, kInstallRowH, count,
                          install_scroll);

        if (focus == Focus::Rail) {
            DrawRailHints(c);
            return;
        }
        int hx = kContentX + 24;
        const int hy = kContentBottom + (kHintH - 26) / 2;
        hx += DrawHint(c, hx, hy, "A", SelectedCia() ? "Install" : "Open") + 22;
        hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
        hx += DrawHint(c, hx, hy, "Y", "Refresh") + 22;
        DrawHint(c, hx, hy, "+ -", "Exit");
    }

    // Name on the left, then size, version and a kind badge packed to the right.
    void DrawCiaRow(Canvas& c, const CiaEntry& cia, int x, int y, int w) {
        const int text_y = CenterBaseline(y, kInstallRowH - 4, 18);
        const int badge_y = y + (kInstallRowH - 4 - 20) / 2;
        const int badge_text_y = CenterBaseline(badge_y, 20, 14);
        int right = x + w - 20;

        const char* badge = cia.readable ? TitleKindName(cia.kind) : "UNREADABLE";
        const u32 badge_col = cia.readable ? KindBadgeColor(cia.kind) : kColError;
        const int bw = g_font.Measure(badge, 14) + 14;
        right -= bw;
        c.FillRoundRect(right, badge_y, bw, 20, 7, badge_col);
        g_font.Draw(c, right + 7, badge_text_y, badge, 14, kColText);
        right -= 12;

        if (cia.readable) {
            const std::string version = FormatTitleVersion(cia.version);
            const int vw = g_font.Measure(version, 16);
            right -= vw;
            g_font.Draw(c, right, text_y, version, 16, kColTextDim);
            right -= 12;
        }

        const std::string size = FormatSize(cia.size);
        const int sw = g_font.Measure(size, 16);
        right -= sw;
        g_font.Draw(c, right, text_y, size, 16, kColTextDim);

        g_font.Draw(c, x + 20, text_y, g_font.Truncate(cia.name, 18, std::max(60, right - x - 32)),
                    18, kColText);
    }

    void DrawConfirmInstall(Canvas& c, const CiaEntry& cia, bool replacing, u16 installed_version) {
        constexpr int w = 620;
        constexpr int h = 268;
        const int x = (kScreenW - w) / 2;
        const int y = (kScreenH - h) / 2;
        c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
        c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

        int ty = y + 20;
        g_font.Draw(c, x + 24, ty + 22, "Install this title?", 24, kColText);
        ty += 36;
        g_font.Draw(c, x + 24, ty + 18, g_font.Truncate(cia.name, 18, w - 48), 18, kColTextDim);
        ty += 32;
        c.FillRect(x + 24, ty, w - 48, 1, kColRail);
        ty += 10;

        const auto row = [&](const char* label, const std::string& value, u32 color) {
            g_font.Draw(c, x + 24, ty + 18, label, 18, kColTextDim);
            g_font.Draw(c, x + 190, ty + 18, g_font.Truncate(value, 18, w - 214), 18, color);
            ty += 28;
        };
        row("Type", TitleKindName(cia.kind), kColText);
        row("Title ID", FormatTitleId(cia.program_id), kColText);
        row("Version", FormatTitleVersion(cia.version), kColText);
        if (replacing) {
            row("Replaces", FormatTitleVersion(installed_version), kColError);
        }

        int hx = x + 24;
        const int hy = y + h - 38;
        hx += DrawHint(c, hx, hy, "A", "Install") + 22;
        DrawHint(c, hx, hy, "B", "Cancel");
    }

    void DrawDecryptWarning(Canvas& c) {
        constexpr int w = 640;
        constexpr int h = 232;
        const int x = (kScreenW - w) / 2;
        const int y = (kScreenH - h) / 2;
        c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
        c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

        int ty = y + 20;
        g_font.Draw(c, x + 24, ty + 22, "Raw .3ds ROM", 24, kColText);
        ty += 44;
        g_font.Draw(c, x + 24, ty + 18, "This looks like a raw .3ds dump, which is usually", 18,
                    kColTextDim);
        ty += 24;
        g_font.Draw(c, x + 24, ty + 18, "still encrypted and won't boot as-is.", 18, kColTextDim);
        ty += 32;
        g_font.Draw(c, x + 24, ty + 18, "Decrypt it first, or use", 18,
                    kColTextDim);
        ty += 24;
        g_font.Draw(c, x + 24, ty + 18, "a .cci/.cxi dump instead.", 18, kColTextDim);

        int hx = x + 24;
        const int hy = y + h - 38;
        hx += DrawHint(c, hx, hy, "A", "Launch anyway") + 22;
        DrawHint(c, hx, hy, "B", "Cancel");
    }

    void DrawConfirmClearShaderCache(Canvas& c, const GameEntry& game) {
        constexpr int w = 620;
        constexpr int h = 200;
        const int x = (kScreenW - w) / 2;
        const int y = (kScreenH - h) / 2;
        c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
        c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

        int ty = y + 20;
        g_font.Draw(c, x + 24, ty + 22, "Clear shader cache?", 24, kColText);
        ty += 40;
        g_font.Draw(c, x + 24, ty + 18, g_font.Truncate(game.title, 18, w - 48), 18, kColTextDim);
        ty += 30;
        g_font.Draw(c, x + 24, ty + 18, "Every cached shader/pipeline for this title is", 18,
                    kColTextDim);
        ty += 24;
        g_font.Draw(c, x + 24, ty + 18, "deleted. It'll recompile from scratch next launch.", 18,
                    kColTextDim);

        int hx = x + 24;
        const int hy = y + h - 38;
        hx += DrawHint(c, hx, hy, "A", "Clear") + 22;
        DrawHint(c, hx, hy, "B", "Cancel");
    }

    void DrawPopup(Canvas& c, const std::string& message, const char* title = "Can't launch",
                   u32 title_color = kColError) {
        constexpr int w = 620;
        constexpr int h = 176;
        const int x = (kScreenW - w) / 2;
        const int y = (kScreenH - h) / 2;
        c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
        c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

        g_font.Draw(c, x + 24, y + 40, title, 24, title_color);
        g_font.Draw(c, x + 24, y + 78, g_font.Truncate(message, 18, w - 48), 18, kColText);

        const int hx = x + 24;
        const int hy = y + h - 38;
        DrawHint(c, hx, hy, "A", "OK");
    }

    void DrawInstallProgress(Canvas& c) {
        const std::size_t written = install_written.load();
        const std::size_t total = install_total.load();
        constexpr int w = 560;
        constexpr int h = 136;
        const int x = (kScreenW - w) / 2;
        const int y = (kScreenH - h) / 2;
        c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
        c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);
        g_font.Draw(c, x + 24, y + 42, g_font.Truncate("Installing " + install_name, 20, w - 48),
                    20, kColText);

        const int bar_x = x + 24;
        const int bar_y = y + 64;
        const int bar_w = w - 48;
        c.FillRoundRect(bar_x, bar_y, bar_w, 10, 5, kColRail);
        const int fill =
            total == 0 ? 0 : static_cast<int>(static_cast<u64>(bar_w) * written / total);
        c.FillRoundRect(bar_x, bar_y, std::clamp(fill, 0, bar_w), 10, 5, kColAccent);

        g_font.Draw(c, bar_x, bar_y + 36, FormatSize(written) + " / " + FormatSize(total), 18,
                    kColTextDim);
        const char* warn = "Don't close Raikopon or turn off the console";
        g_font.Draw(c, x + w - 24 - g_font.Measure(warn, 18), bar_y + 36, warn, 18, kColTextDim);
    }

    void DrawLibrary(Canvas& c) {
        std::string sub;
        if (!search.empty()) {
            sub = "Search: " + search + "  (" + std::to_string(filtered.size()) + ")";
        } else {
            sub = std::to_string(games.size()) + (games.size() == 1 ? " game" : " games");
        }
        DrawHeader(c, sub);

        const bool content_focus = focus == Focus::Content;
        if (filtered.empty()) {
            DrawEmptyLibrary(c, paths.roms_dir);
        } else {
            const Grid grid = ComputeGrid();
            for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
                int x, y;
                if (TileRect(grid, i, x, y)) {
                    DrawTile(c, games[filtered[i]], x, y, i == selected, content_focus);
                }
            }
            DrawScrollbar(c, grid);
        }
        if (focus == Focus::Rail) {
            DrawRailHints(c);
        } else {
            int hx = kContentX + 24;
            const int hy = kContentBottom + (kHintH - 26) / 2;
            hx += DrawHint(c, hx, hy, "A", "Launch") + 22;
            hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
            hx += DrawHint(c, hx, hy, "X", "Search") + 22;
            hx += DrawHint(c, hx, hy, "Y", "Refresh") + 22;
            hx += DrawHint(c, hx, hy, "+", "Details") + 22;
            DrawHint(c, hx, hy, "+ -", "Exit");
        }
    }

    void DrawScrollbar(Canvas& c, const Grid& grid) {
        const int total_rows = (static_cast<int>(filtered.size()) + grid.cols - 1) / grid.cols;
        if (total_rows <= grid.visible_rows) {
            return;
        }
        const int track_h = grid.visible_rows * (kTileH + kTileGap) - kTileGap;
        const int track_x = kScreenW - 10;
        c.FillRoundRect(track_x, grid.top, 4, track_h, 2, kColRail);
        const int thumb_h = std::max(24, track_h * grid.visible_rows / total_rows);
        const int max_scroll = total_rows - grid.visible_rows;
        const int thumb_y = grid.top + (track_h - thumb_h) * scroll_row / std::max(1, max_scroll);
        c.FillRoundRect(track_x, thumb_y, 4, thumb_h, 2, kColAccent);
    }

    static constexpr int kRowH = 41;
    static constexpr int kTabBarH = 44;
    static constexpr int kSettingsFooterH = 100;
    static constexpr int kSettingsTop = kContentTop + kTabBarH + 16;
    static constexpr int kSettingsRows =
        (kContentBottom - kSettingsFooterH - kSettingsTop) / (kRowH + 8);

    void DrawSettingsTabBar(Canvas& c) {
        constexpr int cur_size = 24;
        constexpr int side_size = 18;
        constexpr int gap = 10;
        constexpr int edge_pad = 24;

        const int idx = static_cast<int>(settings_tab);
        const char* cur = kSettingsTabs[idx].second;
        const char* prev = kSettingsTabs[(idx - 1 + kNumSettingsTabs) % kNumSettingsTabs].second;
        const char* next = kSettingsTabs[(idx + 1) % kNumSettingsTabs].second;

        const int band_top = kContentTop;
        const int text_baseline = CenterBaseline(band_top, kTabBarH, side_size);
        const int cur_baseline = CenterBaseline(band_top, kTabBarH, cur_size);
        constexpr int chip_h = 26;
        const int chip_top = text_baseline - (chip_h + static_cast<int>(side_size * 0.7f)) / 2;

        const int cur_w = g_font.Measure(cur, cur_size);
        g_font.Draw(c, kContentX + (kContentW - cur_w) / 2, cur_baseline, cur, cur_size,
                    kColAccent);

        int lx = kContentX + edge_pad;
        lx += DrawButtonChip(c, lx, chip_top, "L") + gap;
        g_font.Draw(c, lx, text_baseline, prev, side_size, kColTextDim);

        const int r_chip_w = std::max(chip_h, g_font.Measure("R", 18) + 16);
        const int next_w = g_font.Measure(next, side_size);
        const int rx = kContentX + kContentW - edge_pad - r_chip_w;
        DrawButtonChip(c, rx, chip_top, "R");
        g_font.Draw(c, rx - gap - next_w, text_baseline, next, side_size, kColTextDim);

        c.FillRect(kContentX, band_top + kTabBarH, kContentW, 1, kColRail);
    }

    void DrawSettingsPage(Canvas& c) {
        DrawHeader(c, "");
        DrawSettingsTabBar(c);
        const bool content_focus = focus == Focus::Content;
        const int x = kContentX + 24;
        const int w = kContentW - 48;
        const int footer_y = kSettingsTop + kSettingsRows * (kRowH + 8) + 8;

        if (settings_tab == SettingsTab::Controls) {
            DrawControlsTab(c, content_focus);
            if (remap_capturing) {
                const auto control = static_cast<SwitchFrontend::MappableControl>(remap_sel);
                const std::string prompt =
                    std::string{"Press a button for "} + SwitchFrontend::ControlName(control) +
                    "...";
                g_font.Draw(c, x + 20, footer_y + 16, prompt, 18, kColAccent);
            } else {
                DrawFlavorSentence(c, x + 20, footer_y + 16, kColTextDim,
                                   {{.text = "Press "},
                                    {.chip = "A"},
                                    {.text = " to remap, "},
                                    {.chip = "Y"},
                                    {.text = " to reset to default."}});
            }
        } else {
            auto rows = BuildSettingRows(settings_tab, settings);
            const int count = static_cast<int>(rows.size());
            const SettingRowIdx current_item = rows[settings_sel].item;
            if (settings_armed && current_item == SettingRowGyroSensitivity) {
                rows[settings_sel].value = GyroSensitivityArmedText(settings, gyro_edit_y);
            }
            for (int i = settings_scroll; i < std::min(count, settings_scroll + kSettingsRows);
                 ++i) {
                const int y = kSettingsTop + (i - settings_scroll) * (kRowH + 8);
                const bool on = i == settings_sel;
                const bool armed_here = on && settings_armed && content_focus;
                if (armed_here) {
                    // Highlight the whole row, not just the value, while armed — makes it
                    // unambiguous that joystick left/right is now live for this row.
                    c.FillRoundRect(x, y, w, kRowH, 10, kColAccent);
                    c.FillRoundRect(x, y + 8, 4, kRowH - 16, 2, kColSurface);
                } else if (on) {
                    c.FillRoundRect(x, y, w, kRowH, 10,
                                    content_focus ? kColSurfaceHi : kColSurface);
                    c.FillRoundRect(x, y + 8, 4, kRowH - 16, 2,
                                    content_focus ? kColAccent : kColBadge);
                }
                g_font.Draw(c, x + 20, CenterBaseline(y, kRowH, 22), rows[i].label, 22,
                            armed_here ? kColSurface : kColText);
                const int vw = g_font.Measure(rows[i].value, 22);
                g_font.Draw(c, x + w - 24 - vw, CenterBaseline(y, kRowH, 22), rows[i].value, 22,
                            armed_here             ? kColSurface
                            : on && content_focus  ? kColAccent
                                                    : kColTextDim);
            }
            DrawListScrollbar(c, kScreenW - 10, kSettingsTop, kSettingsRows, kRowH + 8, count,
                              settings_scroll);

            g_font.Draw(c, x + 20, footer_y + 16, rows[settings_sel].description, 18, kColTextDim);
            // Flavor text explaining the current select-then-adjust interaction, since there's
            // nothing else on screen to tell a first-time player the stick doesn't just work.
            // Button names render as the same rounded chips the hint bar uses, via
            // DrawFlavorSentence.
            if (settings_armed) {
                if (current_item == SettingRowGyroSensitivity) {
                    DrawFlavorSentence(c, x + 20, footer_y + 42, kColAccent,
                                       {{.text = "Adjusting "},
                                        {.chip = gyro_edit_y ? "Y" : "X"},
                                        {.text = " — "},
                                        {.chip = "v"},
                                        {.text = " to swap axis, "},
                                        {.chip = "<>"},
                                        {.text = " changes it, "},
                                        {.chip = "A"},
                                        {.text = " or "},
                                        {.chip = "B"},
                                        {.text = " when done."}});
                } else {
                    DrawFlavorSentence(c, x + 20, footer_y + 42, kColAccent,
                                       {{.text = "Adjusting this value — use "},
                                        {.chip = "<>"},
                                        {.text = " to change it, "},
                                        {.chip = "A"},
                                        {.text = " or "},
                                        {.chip = "B"},
                                        {.text = " when done."}});
                }
            } else if (current_item != SettingRowLayoutCycle) {
                if (IsBooleanSetting(current_item)) {
                    DrawFlavorSentence(
                        c, x + 20, footer_y + 42, kColTextDim,
                        {{.text = "Press "},
                         {.chip = "A"},
                         {.text = " to toggle. The stick only moves the selection."}});
                } else {
                    DrawFlavorSentence(c, x + 20, footer_y + 42, kColTextDim,
                                       {{.text = "Press "},
                                        {.chip = "A"},
                                        {.text = " to select this value, then use "},
                                        {.chip = "<>"},
                                        {.text = " to change it."}});
                }
            }
        }

        if (focus == Focus::Rail) {
            DrawRailHints(c);
        } else if (settings_tab == SettingsTab::Controls) {
            int hx = kContentX + 24;
            const int hy = kContentBottom + (kHintH - 26) / 2;
            if (!remap_capturing) {
                hx += DrawHint(c, hx, hy, "A", "Remap") + 22;
                hx += DrawHint(c, hx, hy, "Y", "Reset") + 22;
                hx += DrawHint(c, hx, hy, "L/R", "Tab") + 22;
                hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
            }
            DrawHint(c, hx, hy, "+ -", "Exit");
        } else {
            const auto rows = BuildSettingRows(settings_tab, settings);
            const SettingRowIdx current_item = rows[settings_sel].item;
            int hx = kContentX + 24;
            const int hy = kContentBottom + (kHintH - 26) / 2;
            if (settings_armed) {
                hx += DrawHint(c, hx, hy, "<>", "Adjust") + 22;
                hx += DrawHint(c, hx, hy, "A / B", "Done") + 22;
            } else {
                if (current_item == SettingRowLayoutCycle) {
                    hx += DrawHint(c, hx, hy, "A", "Configure") + 22;
                } else if (IsBooleanSetting(current_item)) {
                    hx += DrawHint(c, hx, hy, "A", "Toggle") + 22;
                } else {
                    hx += DrawHint(c, hx, hy, "A", "Select") + 22;
                }
                hx += DrawHint(c, hx, hy, "L/R", "Tab") + 22;
                hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
                hx += DrawHint(c, hx, hy, "-", "Reset Defaults") + 22;
            }
            DrawHint(c, hx, hy, "+ -", "Exit");
        }

        if (settings_reset_confirm) {
            DrawConfirmDialog(c, "Reset settings to defaults?",
                              "Every setting on this screen reverts to the values",
                              "Azahar/Raikopon ships with.", "Reset", "Cancel");
        }
    }

    // A centred modal to choose which layouts R3 cycles through in-game.
    void DrawLayoutPicker(Canvas& c) {
        const int count = GetScreenLayoutCount();
        constexpr int w = 620;
        constexpr int row_h = 44;
        constexpr int top_pad = 78;    // Room for the title and subtitle.
        constexpr int bottom_pad = 56; // Room for the button hints.
        const int h = top_pad + count * row_h + bottom_pad;
        const int x = (kScreenW - w) / 2;
        const int y = (kScreenH - h) / 2;
        c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
        c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

        g_font.Draw(c, x + 24, y + 40, "R3 Screen Layouts", 24, kColText);
        g_font.Draw(c, x + 24, y + 64, "Choose which layouts R3 cycles through in-game", 16,
                    kColTextDim);

        for (int i = 0; i < count; ++i) {
            const int ry = y + top_pad + i * row_h;
            const int rx = x + 16;
            const int rw = w - 32;
            if (i == layout_picker_sel) {
                c.FillRoundRect(rx, ry, rw, row_h - 4, 8, kColSurfaceHi);
                c.FillRoundRect(rx, ry + 8, 4, row_h - 20, 2, kColAccent);
            }
            const bool enabled = (settings.layout_cycle_mask & (1u << i)) != 0;
            g_font.Draw(c, rx + 20, CenterBaseline(ry, row_h - 4, 20), GetScreenLayoutName(i), 20,
                        kColText);
            const char* state = enabled ? "On" : "Off";
            const int sw = g_font.Measure(state, 20);
            g_font.Draw(c, rx + rw - 24 - sw, CenterBaseline(ry, row_h - 4, 20), state, 20,
                        enabled ? kColAccent : kColTextDim);
        }

        int hx = x + 24;
        const int hy = y + h - 38;
        hx += DrawHint(c, hx, hy, "A", "Toggle") + 22;
        DrawHint(c, hx, hy, "B", "Done");
    }

    void DrawHintBar(Canvas& c) {
        c.FillRect(0, kContentBottom, kScreenW, kHintH, kColHintBar);
        c.FillRect(0, kContentBottom, kScreenW, 1, kColRail);
    }

    // Legend shown while the cursor sits on the Library/Settings rail.
    void DrawRailHints(Canvas& c) {
        int hx = kContentX + 24;
        const int hy = kContentBottom + (kHintH - 26) / 2;
        hx += DrawHint(c, hx, hy, "^v", "Move") + 22;
        hx += DrawHint(c, hx, hy, "A", "Open") + 22;
        hx += DrawHint(c, hx, hy, "B", "Back") + 22;
        DrawHint(c, hx, hy, "+ -", "Exit");
    }

    // Full-frame busy indicator for the brief blocking scans.
    void ShowBusy(std::string_view msg) {
        Draw();
        canvas.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xB0));
        const int tw = g_font.Measure(msg, 22);
        const int w = tw + 56, h = 56;
        const int x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
        canvas.FillRoundRect(x, y, w, h, 12, kColSurfaceHi);
        g_font.Draw(canvas, x + 28, CenterBaseline(y, h, 22), msg, 22, kColText);
        Present();
    }

    void EnsureFramebuffer() {
        if (fb_ready) {
            return;
        }
        framebufferCreate(&fb, nwindowGetDefault(), kScreenW, kScreenH, PIXEL_FORMAT_RGBA_8888, 2);
        framebufferMakeLinear(&fb);
        fb_ready = true;
    }

    void DrawLoading() {
        canvas.Clear(kColBg);
        const char* msg = "Loading library...";
        const int w = g_font.Measure(msg, 24);
        g_font.Draw(canvas, kContentX + (kContentW - w) / 2, kScreenH / 2, msg, 24, kColTextDim);
    }

    void Present() {
        EnsureFramebuffer();
        u32 stride = 0;
        auto* base = static_cast<u8*>(framebufferBegin(&fb, &stride));
        const u32* src = canvas.Data();
        for (int y = 0; y < kScreenH; ++y) {
            std::memcpy(base + static_cast<std::size_t>(y) * stride, src + y * kScreenW,
                        static_cast<std::size_t>(kScreenW) * 4);
        }
        framebufferEnd(&fb);
    }

    Canvas canvas;
    bool touch_was_down = false;
    int touch_start_x = 0;
    int touch_start_y = 0;
    int touch_last_x = 0;
    int touch_last_y = 0;
    bool touch_swipe_candidate = false;
    // Set by a second touch on the focused tile so Run() can escape the loop.
    std::optional<std::string> pending_launch;
};

} // namespace

MenuResult RunMenu(PadState& pad) {
    if (!g_font.Init()) {
        // Exit on no font found.
        return {MenuAction::Exit, {}};
    }
    Menu menu;
    return menu.Run(pad);
}

void SetMenuNotice(const std::string& text) {
    ShowNotice(text, true);
}

void SetLaunchErrorPopup(const std::string& text) {
    g_pending_popup = text;
}

std::string PromptKeyboard(const std::string& header, const std::string& guide,
                            const std::string& initial, int max_len) {
    return PromptKeyboardText(header, guide, initial, max_len);
}

void ShutdownMenu() {
    g_font.Shutdown();
}

} // namespace SwitchFrontend
