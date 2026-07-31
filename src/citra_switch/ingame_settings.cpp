// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <switch.h>

#include "citra_switch/canvas.h"
#include "citra_switch/config.h"
#include "citra_switch/game_settings.h"
#include "citra_switch/ingame_cheats.h"
#include "citra_switch/ingame_settings.h"
#include "citra_switch/input.h"
#include "citra_switch/layout_editor.h"
#include "citra_switch/menu_data.h"
#include "citra_switch/settings_model.h"

namespace SwitchFrontend {
namespace {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

// The in-game tab set is smaller than the library's: no Controls (remapping doesn't need the
// game paused) and Cheats replaces it instead, since cheats have no library-menu equivalent.
enum class Tab { Graphics, Debug, Misc, Cheats };

// A confirm dialog currently covering the screen, if any. Only one can be open at a time; while
// one is open, every other input path below is suspended.
enum class Modal { None, ResetConfirm, ExitConfirm };
constexpr std::array<std::pair<Tab, const char*>, 4> kTabs{{
    {Tab::Graphics, "Graphics"},
    {Tab::Debug, "Debug"},
    {Tab::Misc, "Misc"},
    {Tab::Cheats, "Cheats"},
}};

SettingsTab ToSettingsTab(Tab t) {
    switch (t) {
    case Tab::Graphics:
        return SettingsTab::Graphics;
    case Tab::Debug:
        return SettingsTab::Debug;
    case Tab::Misc:
        return SettingsTab::Misc;
    default:
        return SettingsTab::Graphics;
    }
}

// Rows without a sensible in-game meaning (boot-time-only fields, CPU JIT, R3 layout cycle) are
// filtered out — see IsPerGameEditable's comment for the exact list.
std::vector<SettingRow> VisibleRows(SettingsTab tab, const MenuSettings& s) {
    auto rows = BuildSettingRows(tab, s);
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const SettingRow& r) { return !IsPerGameEditable(r.item); }),
              rows.end());
    return rows;
}

// Same as VisibleRows, plus synthetic action/state rows with no MenuSettings field of their own:
// "Edit Screen Layout" on the Graphics tab (mirrors the old quick menu's Display-tab EditLayout
// row, launching the layout editor — which also owns each screen's opacity, see layout_editor.cpp
// — instead of editing a setting), and Movie Throttle Clock / Touch Pointer on the Debug tab
// (mirror the old quick menu's rows of the same name — see emulation.cpp's
// Get/SetMovieThrottleClockPercentage and input.h's Is/TogglePointerMode).
std::vector<SettingRow> RowsForTab(Tab tab, const MenuSettings& s) {
    auto rows = VisibleRows(ToSettingsTab(tab), s);
    if (tab == Tab::Graphics) {
        rows.push_back({SettingRowEditLayout, "Edit Screen Layout", "",
                        "Move, resize, and set the opacity of the top/bottom screens."});
    } else if (tab == Tab::Debug) {
        // Inserted right after CPU Clock (rather than appended at the end) since it's the same
        // kind of setting - the Core Clock percentage, just for while a cutscene is playing.
        const auto cpu_clock_it = std::find_if(
            rows.begin(), rows.end(), [](const SettingRow& r) { return r.item == SettingRowCpuClock; });
        rows.insert(cpu_clock_it == rows.end() ? rows.end() : cpu_clock_it + 1,
                    {SettingRowMovieThrottle, "Movie Throttle Clock",
                     std::to_string(GetMovieThrottleClockPercentage()) + "%",
                     "Core Clock used while a movie-library cutscene is playing."});
        rows.push_back({SettingRowPointerMode, "Touch Pointer",
                        IsPointerModeActive() ? "On" : "Off",
                        "Enable the virtual touch cursor driven by Touch Pointer Source."});
    }
    return rows;
}

// ---- Cheats tab -----------------------------------------------------------------------------
// CheatEngine access itself lives in ingame_cheats.h/.cpp — a separate, core-side translation
// unit — since this file owns a native Framebuffer/PadState and can't include core headers in
// the same TU (see ReleaseWindowForMenu's comment in emulation.cpp). Only pagination math and
// selection bookkeeping live here.

constexpr int kCheatsPerPage = 8;

// EditCheatFlow/DeleteCheatFlow return the affected cheat's plain index (or -1 if the add/edit
// flow was cancelled); this converts that into the (page, row-within-page) the UI tracks.
void SelectCheat(int cheat_index, int& selected, int& page) {
    if (cheat_index < 0) {
        return;
    }
    page = cheat_index / kCheatsPerPage;
    selected = 1 + (cheat_index - page * kCheatsPerPage); // +1 for the pinned Add Cheat row.
}

// ---- Layout / drawing ------------------------------------------------------------------------

constexpr int kContentX = 64;
constexpr int kContentW = kScreenW - 2 * kContentX;
constexpr int kTabBarTop = 48;
constexpr int kTabBarH = 56;
constexpr int kRowsTop = kTabBarTop + kTabBarH + 24;
constexpr int kRowH = 52;
constexpr int kRowGap = 8;
constexpr int kVisibleRows = (kScreenH - kRowsTop - 140) / (kRowH + kRowGap);
constexpr int kFooterY = kScreenH - 100;

void DrawTabBar(Canvas& c, Tab tab) {
    constexpr int cur_size = 24;
    constexpr int side_size = 18;
    constexpr int gap = 10;

    const int idx = static_cast<int>(tab);
    const char* cur = kTabs[idx].second;
    const char* prev = kTabs[(idx - 1 + 4) % 4].second;
    const char* next = kTabs[(idx + 1) % 4].second;

    const int text_baseline = CenterBaseline(kTabBarTop, kTabBarH, side_size);
    const int cur_baseline = CenterBaseline(kTabBarTop, kTabBarH, cur_size);
    constexpr int chip_h = 26;
    const int chip_top = text_baseline - (chip_h + static_cast<int>(side_size * 0.7f)) / 2;

    Font& font = GetSharedFont();
    const int cur_w = font.Measure(cur, cur_size);
    font.Draw(c, kContentX + (kContentW - cur_w) / 2, cur_baseline, cur, cur_size, kColAccent);

    int lx = kContentX;
    lx += DrawButtonChip(c, lx, chip_top, "L") + gap;
    font.Draw(c, lx, text_baseline, prev, side_size, kColTextDim);

    const int r_chip_w = std::max(chip_h, font.Measure("R", 18) + 16);
    const int next_w = font.Measure(next, side_size);
    const int rx = kContentX + kContentW - r_chip_w;
    DrawButtonChip(c, rx, chip_top, "R");
    font.Draw(c, rx - gap - next_w, text_baseline, next, side_size, kColTextDim);

    c.FillRect(kContentX, kTabBarTop + kTabBarH, kContentW, 1, kColRail);
}

void DrawFooterHints(Canvas& c, bool show_cheat_paging) {
    int x = kContentX;
    x += DrawHint(c, x, kFooterY, "B", "Close") + 32;
    x += DrawHint(c, x, kFooterY, "+", "Quit Game") + 32;
    x += DrawHint(c, x, kFooterY, "-", "Reset to Library") + 32;
    x += DrawHint(c, x, kFooterY, "L/R", "Tabs") + 32;
    if (show_cheat_paging) {
        x += DrawHint(c, x, kFooterY, "ZL/ZR", "Cheat list") + 32;
    }
    DrawHint(c, x, kFooterY, "A", "Select / toggle");
}

// Draws the Graphics/Debug/Misc row list for `tab`. Mirrors menu.cpp's DrawSettingsPage row
// styling so the in-game screen reads as the same UI, just full-screen instead of rail+content.
void DrawSettingRows(Canvas& c, const std::vector<SettingRow>& rows, int selected, bool armed,
                     bool gyro_edit_y) {
    Font& font = GetSharedFont();
    const int count = static_cast<int>(rows.size());
    if (count == 0) {
        font.Draw(c, kContentX, kRowsTop + 40, "Nothing to show on this tab.", 20, kColTextDim);
        return;
    }
    const int scroll =
        std::clamp(selected - kVisibleRows / 2, 0, std::max(0, count - kVisibleRows));
    for (int i = scroll; i < std::min(count, scroll + kVisibleRows); ++i) {
        const int y = kRowsTop + (i - scroll) * (kRowH + kRowGap);
        const bool on = i == selected;
        const bool armed_here = on && armed;
        if (armed_here) {
            c.FillRoundRect(kContentX, y, kContentW, kRowH, 10, kColAccent);
        } else if (on) {
            c.FillRoundRect(kContentX, y, kContentW, kRowH, 10, kColSurfaceHi);
            c.FillRoundRect(kContentX, y + 8, 4, kRowH - 16, 2, kColAccent);
        }
        std::string value = rows[i].value;
        if (armed_here && rows[i].item == SettingRowGyroSensitivity) {
            // Caller already knows the axis; the value string just needs to reflect it.
            value = gyro_edit_y ? ("X …   [Y]") : ("[X]   Y …");
        }
        font.Draw(c, kContentX + 20, CenterBaseline(y, kRowH, 22), rows[i].label, 22,
                  armed_here ? kColOnAccent : kColText);
        const int vw = font.Measure(value, 22);
        font.Draw(c, kContentX + kContentW - 24 - vw, CenterBaseline(y, kRowH, 22), value, 22,
                  armed_here ? kColOnAccent : (on ? kColAccent : kColTextDim));
    }
    DrawListScrollbar(c, kContentX + kContentW + 10, kRowsTop, kVisibleRows, kRowH + kRowGap,
                      count, scroll);
    font.Draw(c, kContentX, kFooterY - 34, rows[selected].description, 18, kColTextDim);
}

void DrawCheatsTab(Canvas& c, int selected, int page) {
    Font& font = GetSharedFont();
    const int count = CheatCount();
    const int page_count = count <= 0 ? 1 : (count + kCheatsPerPage - 1) / kCheatsPerPage;
    const int first = page * kCheatsPerPage;
    const int last = std::min(count, first + kCheatsPerPage);

    struct Line {
        std::string label;
        std::string value;
        bool is_add;
        int cheat_index;
    };
    std::vector<Line> lines;
    lines.push_back({"+ Add Cheat", "", true, -1});
    for (int i = first; i < last; ++i) {
        lines.push_back({CheatName(i), CheatEnabled(i) ? "On" : "Off", false, i});
    }
    if (count == 0) {
        lines.push_back({"No cheats loaded for this game.", "", false, -1});
    }

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const int y = kRowsTop + i * (kRowH + kRowGap);
        const bool on = i == selected;
        if (on) {
            c.FillRoundRect(kContentX, y, kContentW, kRowH, 10, kColSurfaceHi);
            c.FillRoundRect(kContentX, y + 8, 4, kRowH - 16, 2, kColAccent);
        }
        font.Draw(c, kContentX + 20, CenterBaseline(y, kRowH, 22), lines[i].label, 22,
                  lines[i].is_add ? kColAccent : kColText);
        if (!lines[i].value.empty()) {
            const int vw = font.Measure(lines[i].value, 22);
            font.Draw(c, kContentX + kContentW - 24 - vw, CenterBaseline(y, kRowH, 22),
                      lines[i].value, 22, on ? kColAccent : kColTextDim);
        }
    }

    if (page_count > 1) {
        const std::string page_text =
            "List " + std::to_string(page + 1) + "/" + std::to_string(page_count);
        font.Draw(c, kContentX, kFooterY - 34, page_text, 18, kColTextDim);
    }
    DrawFlavorSentence(c, kContentX + (page_count > 1 ? 140 : 0), kFooterY - 34 + 14, kColTextDim,
                       {{.text = "Press "},
                        {.chip = "A"},
                        {.text = " to toggle, "},
                        {.chip = "X"},
                        {.text = " to edit, "},
                        {.chip = "Y"},
                        {.text = " to delete."}});
}

} // namespace

bool ShowInGameSettings(PadState& pad) {
    if (!ReleaseWindowForMenu()) {
        return false;
    }

    // TEMPORARY diagnostics: a report of a crash right around here had no further log output at
    // all (not even a signal/stack trace), consistent with a hard native crash. These checkpoints
    // cover the whole first-time-through-the-loop path in one pass so a recurrence pinpoints the
    // exact failing call without needing another round of user testing. Only the entry/exit
    // checkpoints (which each fire once regardless) and the first loop iteration's checkpoints
    // (guarded by first_frame) are logged - not logged every frame after that, since ProbeLog's
    // 30ms flush-sync sleep would make the whole menu laggy for as long as it stays open.
    ProbeLog("ingame settings: creating framebuffer");
    Framebuffer fb{};
    framebufferCreate(&fb, nwindowGetDefault(), kScreenW, kScreenH, PIXEL_FORMAT_RGBA_8888, 2);
    ProbeLog("ingame settings: framebuffer created, making linear");
    framebufferMakeLinear(&fb);
    ProbeLog("ingame settings: framebuffer linear, constructing canvas");
    Canvas canvas;
    ProbeLog("ingame settings: canvas constructed, reading menu settings");

    MenuSettings before = GetMenuSettings();
    MenuSettings settings = before;

    Tab tab = Tab::Graphics;
    int selected = 0;
    bool armed = false;
    bool gyro_edit_y = false;
    Repeater repeater;

    int cheat_selected = 0;
    int cheat_page = 0;

    Modal modal = Modal::None;
    bool closed = false;
    bool want_exit = false;
    bool open_layout_editor_after = false;
    bool first_frame = true; // Gates the loop-body checkpoints below to just the first pass.
    // A prior crash report tied to docking the console while this screen was open landed in a
    // frame past the one the checkpoints above cover, with no diagnostic trace of what came
    // after. appletGetOperationMode() is a cheap poll (no IPC), so this logs only on an actual
    // dock/undock transition - near-zero overhead the rest of the time, but it will catch the
    // exact moment of the transition if that hypothesis is right. The frame counter below bounds
    // the remaining blind spot in general (e.g. if it turns out unrelated to docking) to about
    // two seconds without making the menu laggy the way logging every frame would.
    AppletOperationMode last_op_mode = appletGetOperationMode();
    int frame_counter = 0;
    ProbeLog("ingame settings: menu settings read, entering loop");
    while (!closed && appletMainLoop()) {
        const AppletOperationMode op_mode = appletGetOperationMode();
        if (op_mode != last_op_mode) {
            ProbeLog(op_mode == AppletOperationMode_Console
                         ? "ingame settings: operation mode changed to docked"
                         : "ingame settings: operation mode changed to handheld");
            last_op_mode = op_mode;
        }
        ++frame_counter;
        if (frame_counter % 120 == 0) {
            ProbeLog(("ingame settings: heartbeat, frame " + std::to_string(frame_counter)).c_str());
        }

        if (first_frame) {
            ProbeLog("ingame settings: first frame, polling input");
        }
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        const HidAnalogStickState ls = padGetStickPos(&pad, 0);
        constexpr int dz = 12000;
        const u32 nav = repeater.Step(ls.y > dz || (held & HidNpadButton_Up) != 0,
                                      ls.y < -dz || (held & HidNpadButton_Down) != 0,
                                      ls.x < -dz || (held & HidNpadButton_Left) != 0,
                                      ls.x > dz || (held & HidNpadButton_Right) != 0);

        if (modal == Modal::ExitConfirm) {
            if (down & HidNpadButton_A) {
                want_exit = true;
                closed = true;
            } else if (down & HidNpadButton_B) {
                modal = Modal::None;
            }
        } else if (modal == Modal::ResetConfirm) {
            if (down & HidNpadButton_A) {
                ResetGameOverridesToLibrary();
                settings = GetMenuSettings();
                before = settings;
                modal = Modal::None;
            } else if (down & HidNpadButton_B) {
                modal = Modal::None;
            }
        } else if (down & HidNpadButton_Plus) {
            modal = Modal::ExitConfirm;
        } else if (!armed && (down & HidNpadButton_Minus)) {
            modal = Modal::ResetConfirm;
        } else if (!armed && (down & HidNpadButton_L)) {
            tab = static_cast<Tab>((static_cast<int>(tab) - 1 + 4) % 4);
            selected = 0;
        } else if (!armed && (down & HidNpadButton_R)) {
            tab = static_cast<Tab>((static_cast<int>(tab) + 1) % 4);
            selected = 0;
        } else if (tab == Tab::Cheats) {
            const int count = CheatCount();
            const int page_count = count <= 0 ? 1 : (count + kCheatsPerPage - 1) / kCheatsPerPage;
            const int rows_this_page =
                1 + (count == 0 ? 1 : std::min(count - cheat_page * kCheatsPerPage,
                                               kCheatsPerPage));

            if (down & HidNpadButton_ZL) {
                cheat_page = (cheat_page - 1 + page_count) % page_count;
                cheat_selected = std::min(cheat_selected, rows_this_page - 1);
            } else if (down & HidNpadButton_ZR) {
                cheat_page = (cheat_page + 1) % page_count;
                cheat_selected = std::min(cheat_selected, rows_this_page - 1);
            } else {
                if (nav & DirUp) {
                    cheat_selected = std::max(0, cheat_selected - 1);
                }
                if (nav & DirDown) {
                    cheat_selected = std::min(rows_this_page - 1, cheat_selected + 1);
                }
                const int this_cheat_index =
                    cheat_selected == 0 ? -1 : cheat_page * kCheatsPerPage + cheat_selected - 1;
                if (down & HidNpadButton_A) {
                    if (this_cheat_index < 0) {
                        SelectCheat(EditCheatFlow(-1), cheat_selected, cheat_page);
                    } else {
                        ToggleCheat(this_cheat_index);
                    }
                }
                if ((down & HidNpadButton_X) && this_cheat_index >= 0) {
                    SelectCheat(EditCheatFlow(this_cheat_index), cheat_selected, cheat_page);
                }
                if ((down & HidNpadButton_Y) && this_cheat_index >= 0) {
                    SelectCheat(DeleteCheatFlow(this_cheat_index), cheat_selected, cheat_page);
                }
                if (down & HidNpadButton_B) {
                    closed = true;
                }
            }
        } else {
            auto rows = RowsForTab(tab, settings);
            const int count = static_cast<int>(rows.size());
            selected = count == 0 ? 0 : std::clamp(selected, 0, count - 1);

            if (armed) {
                const SettingRowIdx current_item = rows[selected].item;
                if (current_item == SettingRowGyroSensitivity) {
                    if (nav & DirUp) {
                        gyro_edit_y = false;
                    }
                    if (nav & DirDown) {
                        gyro_edit_y = true;
                    }
                    if (nav & DirLeft) {
                        AdjustGyroAxis(settings, gyro_edit_y, -1);
                    }
                    if (nav & DirRight) {
                        AdjustGyroAxis(settings, gyro_edit_y, +1);
                    }
                } else if (current_item == SettingRowMovieThrottle) {
                    if (nav & DirLeft) {
                        SetMovieThrottleClockPercentage(GetMovieThrottleClockPercentage() - 1);
                        MarkGameOverride(OverrideField::MovieThrottleClock);
                    }
                    if (nav & DirRight) {
                        SetMovieThrottleClockPercentage(GetMovieThrottleClockPercentage() + 1);
                        MarkGameOverride(OverrideField::MovieThrottleClock);
                    }
                } else {
                    if (nav & DirLeft) {
                        CycleSetting(settings, current_item, -1);
                    }
                    if (nav & DirRight) {
                        CycleSetting(settings, current_item, +1);
                    }
                }
                if (down & (HidNpadButton_A | HidNpadButton_B)) {
                    armed = false;
                }
            } else {
                if (nav & DirUp) {
                    selected = std::max(0, selected - 1);
                }
                if (nav & DirDown) {
                    selected = std::min(count - 1, selected + 1);
                }
                if (count > 0 && (down & HidNpadButton_A)) {
                    const SettingRowIdx current_item = rows[selected].item;
                    if (current_item == SettingRowEditLayout) {
                        open_layout_editor_after = true;
                        closed = true;
                    } else if (current_item == SettingRowPointerMode) {
                        TogglePointerMode();
                    } else if (IsBooleanSetting(current_item)) {
                        ToggleSetting(settings, current_item);
                    } else {
                        armed = true;
                        gyro_edit_y = false;
                    }
                }
                if (down & HidNpadButton_B) {
                    closed = true;
                }
            }
        }

        if (first_frame) {
            ProbeLog("ingame settings: first frame, input polled, drawing");
        }
        canvas.Clear(kColBg);
        Font& font = GetSharedFont();
        font.Draw(canvas, kContentX, 40, "Settings", 28, kColText);
        DrawTabBar(canvas, tab);
        if (tab == Tab::Cheats) {
            DrawCheatsTab(canvas, cheat_selected, cheat_page);
        } else {
            DrawSettingRows(canvas, RowsForTab(tab, settings), selected, armed, gyro_edit_y);
        }
        DrawFooterHints(canvas, tab == Tab::Cheats && CheatCount() > kCheatsPerPage);
        if (modal == Modal::ExitConfirm) {
            DrawConfirmDialog(canvas, "Quit game?", "Any progress not saved in-game will be lost.",
                              nullptr, "Quit", "Cancel");
        } else if (modal == Modal::ResetConfirm) {
            DrawConfirmDialog(canvas, "Reset to library values?",
                              "Every per-game override for this title will be discarded.", nullptr,
                              "Reset", "Cancel");
        }

        if (first_frame) {
            ProbeLog("ingame settings: first frame, drawn, beginning native framebuffer");
        }
        u32 stride = 0;
        auto* dst = static_cast<std::uint8_t*>(framebufferBegin(&fb, &stride));
        if (dst != nullptr) {
            if (first_frame) {
                ProbeLog("ingame settings: first frame, framebuffer begun, copying pixels");
            }
            const u32* src = canvas.Data();
            for (int y = 0; y < kScreenH; ++y) {
                std::memcpy(dst + static_cast<std::size_t>(y) * stride, src + y * kScreenW,
                            static_cast<std::size_t>(kScreenW) * 4);
            }
            if (first_frame) {
                ProbeLog("ingame settings: first frame, pixels copied, ending framebuffer");
            }
            framebufferEnd(&fb);
        }
        if (first_frame) {
            ProbeLog("ingame settings: first frame complete");
            first_frame = false;
        }
    }

    ProbeLog("ingame settings: loop exited, committing settings");
    CommitMenuSettingsPerGame(before, settings);
    PersistCheats();
    ProbeLog("ingame settings: settings committed, closing framebuffer");
    framebufferClose(&fb);
    ProbeLog("ingame settings: framebuffer closed, reclaiming window");
    ReclaimWindowFromMenu();
    ProbeLog("ingame settings: window reclaimed");

    // Mirrors the old quick menu's EditLayout handling: resume first (just done above via
    // ReclaimWindowFromMenu), then let OpenLayoutEditor re-pause and take over — it draws through
    // the Vulkan overlay pipeline (still live now that the window's back), driven by RunGame's
    // existing editor_open branch, not by this screen.
    if (open_layout_editor_after) {
        OpenLayoutEditor();
    }
    return want_exit;
}

} // namespace SwitchFrontend
