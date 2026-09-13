// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_updates.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <INIReader.h>

#include "citra_switch/config.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"
#include "citra_switch/whats_next.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "raikopon_version.h"

namespace SwitchFrontend {

namespace {

constexpr float kRowHeight = 48.0f;
constexpr float kRowPadding = 6.0f;

bool s_auto_check_loaded = false;
bool s_auto_check_enabled = true;

std::string AutoCheckSettingsFile() {
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "updates_settings.ini";
}

void LoadAutoCheckSetting() {
    s_auto_check_loaded = true;
    s_auto_check_enabled = true;

    const std::string path = AutoCheckSettingsFile();
    if (!FileUtil::Exists(path)) {
        return;
    }
    std::string buffer;
    if (!FileUtil::ReadFileToString(true, path, buffer)) {
        return;
    }
    INIReader ini{buffer.c_str(), buffer.size()};
    if (ini.ParseError() < 0) {
        LOG_ERROR(Config, "Malformed updates settings file '{}'", path);
        return;
    }
    s_auto_check_enabled = ini.GetBoolean("Updates", "auto_check", true);
}

std::thread s_check_thread;
std::atomic<bool> s_check_ready{false};
bool s_check_running = false;
UpdateCheckOutcome s_worker_outcome{};

bool s_have_result = false;
UpdateCheckOutcome s_last_outcome{};
bool s_fresh_result = false;

std::string ResultSummary(UpdateCheckResult result) {
    switch (result) {
    case UpdateCheckResult::UpdateAvailable:
        return Tr("updates.result.available");
    case UpdateCheckResult::UpToDate:
        return Tr("updates.result.uptodate");
    case UpdateCheckResult::NoReleaseFound:
        return Tr("updates.result.norelease");
    case UpdateCheckResult::NetworkError:
        return Tr("updates.result.network_error");
    case UpdateCheckResult::ParseError:
        return Tr("updates.result.parse_error");
    }
    return "";
}

constexpr float kChangelogPopupWidth = 640.0f;
constexpr float kChangelogPopupMaxHeight = 480.0f;
constexpr float kChangelogPopupPaddingX = 24.0f;
constexpr float kChangelogPopupPaddingY = 24.0f;

} // namespace

// Fixed-size overlay (not a full-screen replace) showing the offered release's changelog, parsed
// by updater.cpp's ExtractChangelog from the GitHub release body's "## ... ##" block. Scrolls by
// line once the wrapped text exceeds the popup's max height; closes back to exactly the Updates
// tab underneath, which stays untouched. Exported (not file-local) because it must be checked in
// ui_settings.cpp's UpdateSettings before its rail-focus/ZL-ZR handling - same reasoning as
// cover_download_open/layout_cycle_picker_open there, so those inputs close the popup instead of
// switching rails out from under it and leaving changelog_popup_open stuck true.
void DrawChangelogPopup(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                        GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});

    std::vector<std::string> lines;
    const float body_w = kChangelogPopupWidth - kChangelogPopupPaddingX * 2.0f;
    for (const std::string& raw_line :
        Common::SplitString(LastUpdateCheckResult().info.changelog, '\n')) {
        if (raw_line.empty()) {
            lines.emplace_back();
            continue;
        }
        for (std::string& wrapped : WrapText(canvas, atlas, raw_line, body_w)) {
            lines.push_back(std::move(wrapped));
        }
    }

    const float title_h = atlas.LineHeight() + 12.0f;
    const float max_body_h = kChangelogPopupMaxHeight - kChangelogPopupPaddingY * 2.0f - title_h;
    const int max_visible_lines = std::max(1, static_cast<int>(max_body_h / atlas.LineHeight()));
    const int total_lines = static_cast<int>(lines.size());
    const int visible_lines = std::min(total_lines, max_visible_lines);

    const int max_scroll = std::max(0, total_lines - max_visible_lines);
    model.changelog_scroll_line = std::clamp(model.changelog_scroll_line, 0, max_scroll);
    if (input.up && model.changelog_scroll_line > 0) {
        --model.changelog_scroll_line;
    }
    if (input.down && model.changelog_scroll_line < max_scroll) {
        ++model.changelog_scroll_line;
    }
    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.close")};
    model.hint_chip_count = 1;
    if (input.cancel || input.plus) {
        model.changelog_popup_open = false;
        model.changelog_scroll_line = 0;
        return;
    }

    const float panel_h = kChangelogPopupPaddingY * 2.0f + title_h +
                          static_cast<float>(visible_lines) * atlas.LineHeight();
    const float panel_x = (screen_w - kChangelogPopupWidth) * 0.5f;
    const float panel_y = (screen_h - panel_h) * 0.5f;
    constexpr float kOutlineWidth = 2.0f;
    canvas.DrawQuad(panel_x - kOutlineWidth, panel_y - kOutlineWidth,
                    kChangelogPopupWidth + kOutlineWidth * 2.0f, panel_h + kOutlineWidth * 2.0f,
                    palette.accent);
    canvas.DrawQuad(panel_x, panel_y, kChangelogPopupWidth, panel_h, palette.surface);

    DrawText(canvas, atlas, panel_x + kChangelogPopupPaddingX, panel_y + kChangelogPopupPaddingY,
            Tr("updates.changelog"), palette.accent);

    float line_y = panel_y + kChangelogPopupPaddingY + title_h;
    for (int i = 0; i < visible_lines; ++i) {
        const std::string& line = lines[static_cast<std::size_t>(model.changelog_scroll_line + i)];
        DrawText(canvas, atlas, panel_x + kChangelogPopupPaddingX, line_y, line, palette.text);
        line_y += atlas.LineHeight();
    }
}

bool GetAutoCheckUpdatesEnabled() {
    if (!s_auto_check_loaded) {
        LoadAutoCheckSetting();
    }
    return s_auto_check_enabled;
}

void SetAutoCheckUpdatesEnabled(bool enabled) {
    s_auto_check_loaded = true;
    s_auto_check_enabled = enabled;

    const std::string path = AutoCheckSettingsFile();
    FileUtil::CreateFullPath(path);
    const std::string contents =
        std::string("[Updates]\nauto_check = ") + (enabled ? "true" : "false") + "\n";
    if (!FileUtil::WriteStringToFile(true, path, contents)) {
        LOG_ERROR(Config, "Failed to save updates settings to '{}'", path);
    }
}

void StartUpdateCheck(UpdateChannel channel) {
    if (s_check_running) {
        return;
    }
    if (s_check_thread.joinable()) {
        s_check_thread.join();
    }
    s_check_running = true;
    s_check_ready.store(false, std::memory_order_relaxed);
    s_check_thread = std::thread([channel] {
        s_worker_outcome = CheckForUpdate(channel);
        s_check_ready.store(true, std::memory_order_release);
    });
}

bool IsUpdateCheckPending() {
    return s_check_running;
}

bool ConsumeFreshUpdateCheckResult(UpdateCheckOutcome& outcome) {
    if (s_check_running && s_check_ready.load(std::memory_order_acquire)) {
        s_check_thread.join();
        s_check_running = false;
        s_last_outcome = std::move(s_worker_outcome);
        s_have_result = true;
        s_fresh_result = true;
    }
    if (s_fresh_result) {
        s_fresh_result = false;
        outcome = s_last_outcome;
        return true;
    }
    return false;
}

bool HasUpdateCheckResult() {
    UpdateCheckOutcome unused{};
    ConsumeFreshUpdateCheckResult(unused);
    return s_have_result;
}

const UpdateCheckOutcome& LastUpdateCheckResult() {
    return s_last_outcome;
}

void UpdateUpdatesTab(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                     float dt, float content_x, float content_top, float content_w,
                     float viewport_bottom) {
    const UiPalette& palette = CurrentPalette();
    (void)dt;

    UpdateCheckOutcome fresh{};
    ConsumeFreshUpdateCheckResult(fresh);

    const std::string version_line = Tr("updates.version_prefix") + kRaikoponVersion;
    DrawText(canvas, atlas, content_x, content_top, version_line, palette.text_dim);
    const float list_top = content_top + atlas.LineHeight() + kContentPadding * 0.5f;

    const bool update_available =
        HasUpdateCheckResult() && LastUpdateCheckResult().result == UpdateCheckResult::UpdateAvailable;
    const bool has_changelog = update_available && !LastUpdateCheckResult().info.changelog.empty();
    const int row_count = 3 + (update_available ? 1 : 0) + (has_changelog ? 1 : 0);
    model.updates_row = std::clamp(model.updates_row, 0, row_count - 1);

    model.hint_chips[0] = {PrimaryButtonLabel(MenuAction::Confirm), Tr("hint.select")};
    model.hint_chips[1] = {PrimaryButtonLabel(MenuAction::TabPrev), Tr("hint.prev_tab")};
    model.hint_chips[2] = {PrimaryButtonLabel(MenuAction::TabNext), Tr("hint.next_tab")};
    model.hint_chips[3] = {PrimaryButtonLabel(MenuAction::Cancel), Tr("hint.back")};
    model.hint_chip_count = 4;

    if (input.up && model.updates_row > 0) {
        --model.updates_row;
    }
    if (input.down && model.updates_row + 1 < row_count) {
        ++model.updates_row;
    }

    const UpdateChannel channel = GetUpdateChannel();
    const bool auto_check = GetAutoCheckUpdatesEnabled();

    int activated = -1;
    if (input.confirm) {
        activated = model.updates_row;
    }

    int tapped_row = -1;
    float y = list_top;
    for (int i = 0; i < row_count; ++i) {
        const bool selected = i == model.updates_row;
        if (selected) {
            canvas.DrawQuad(content_x, y, content_w, kRowHeight, palette.surface_warm);
        }
        if (input.touch_tap && input.touch_x >= content_x && input.touch_x < content_x + content_w &&
            input.touch_y >= y && input.touch_y < y + kRowHeight) {
            tapped_row = i;
        }

        const float label_y = y + (kRowHeight - atlas.LineHeight()) * 0.5f;
        const CanvasColor label_color = selected ? palette.text : palette.text_dim;
        std::string label;
        std::string value;
        if (i == 0) {
            label = Tr("updates.row.auto_check");
            value = auto_check ? Tr("common.on") : Tr("common.off");
        } else if (i == 1) {
            label = Tr("updates.row.channel");
            value = channel == UpdateChannel::Stable ? Tr("updates.channel.stable") : Tr("updates.channel.experimental");
        } else if (i == 2) {
            label = Tr("updates.row.check_now");
            value = IsUpdateCheckPending() ? Tr("updates.checking") : "";
        } else if (i == 3 && update_available) {
            label = Tr("updates.row.install_update");
            value = LastUpdateCheckResult().info.tag_name;
        } else {
            label = Tr("updates.row.view_changelog");
            value = "";
        }

        DrawText(canvas, atlas, content_x + kContentPadding, label_y, label, label_color);
        if (i == 2 && IsUpdateCheckPending()) {
            const float pulse = (std::sin(model.wave_elapsed * 2.4f) + 1.0f) * 0.5f;
            const CanvasColor dot_color{palette.accent.r, palette.accent.g, palette.accent.b,
                                        0.4f + 0.6f * pulse};
            const float value_width = MeasureText(atlas, canvas, value);
            DrawText(canvas, atlas, content_x + content_w - kContentPadding - value_width, label_y,
                    value, dot_color);
        } else if (!value.empty()) {
            const float value_width = MeasureText(atlas, canvas, value);
            DrawText(canvas, atlas, content_x + content_w - kContentPadding - value_width, label_y,
                    value, label_color);
        }

        y += kRowHeight + kRowPadding;
    }

    if (tapped_row >= 0) {
        model.updates_row = tapped_row;
        activated = tapped_row;
    }

    if (activated == 0) {
        SetAutoCheckUpdatesEnabled(!auto_check);
    } else if (activated == 1) {
        SetUpdateChannel(channel == UpdateChannel::Stable ? UpdateChannel::Experimental
                                                          : UpdateChannel::Stable);
    } else if (activated == 2 && !IsUpdateCheckPending()) {
        StartUpdateCheck(channel);
    } else if (activated == 3 && update_available) {
        model.pending_update_install = true;
    } else if (activated == 4 && has_changelog) {
        model.changelog_popup_open = true;
    }

    y += kContentPadding * 0.5f;
    if (HasUpdateCheckResult() && y + atlas.LineHeight() <= viewport_bottom) {
        const UpdateCheckOutcome& outcome = LastUpdateCheckResult();
        const CanvasColor result_color = outcome.result == UpdateCheckResult::UpdateAvailable
                                             ? palette.accent
                                             : palette.text_dim;
        std::string summary = ResultSummary(outcome.result);
        if (outcome.result == UpdateCheckResult::UpdateAvailable) {
            summary += ": " + outcome.info.tag_name;
        } else if ((outcome.result == UpdateCheckResult::NetworkError ||
                   outcome.result == UpdateCheckResult::ParseError) &&
                  !outcome.diagnostic.empty()) {
            summary += " (" + outcome.diagnostic + ")";
        }
        DrawText(canvas, atlas, content_x, y, summary, result_color);
        y += atlas.LineHeight() + kContentPadding * 0.5f;
    }

    if (y + atlas.LineHeight() * 2.0f <= viewport_bottom) {
        y += kContentPadding * 0.5f;
        DrawText(canvas, atlas, content_x, y, Tr("updates.whats_next"), palette.accent);
        y += atlas.LineHeight() + 2.0f;
        canvas.DrawQuad(content_x, y, content_w, 2.0f, palette.accent);
        y += kContentPadding * 0.5f;

        EnsureWhatsNextLoaded();
        const std::vector<WhatsNextItem>& items = WhatsNextItems();
        if (items.empty()) {
            DrawText(canvas, atlas, content_x, y, Tr("updates.nothing_planned"), palette.text_dim);
        } else {
            canvas.SetClipRect(content_x, y, content_w, viewport_bottom - y);
            for (const WhatsNextItem& item : items) {
                if (y + atlas.LineHeight() > viewport_bottom) {
                    break;
                }
                const CanvasColor item_color = item.done ? palette.text_dim : palette.text;
                const std::vector<std::string> lines =
                    WrapText(canvas, atlas, item.text, content_w - kContentPadding);
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    if (y + atlas.LineHeight() > viewport_bottom) {
                        break;
                    }
                    const std::string line = (i == 0 ? std::string("- ") : std::string("  ")) + lines[i];
                    DrawText(canvas, atlas, content_x, y, line, item_color);
                    if (item.done) {
                        const float line_width = MeasureText(atlas, canvas, line);
                        canvas.DrawQuad(content_x, y + atlas.LineHeight() * 0.5f, line_width, 1.5f,
                                        palette.text_dim);
                    }
                    y += atlas.LineHeight();
                }
            }
            canvas.ClearClipRect();
        }
    }
}

void RunUpdateInstall(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas, const UpdateInfo& info) {
    const UiPalette& palette = CurrentPalette();

    using Clock = std::chrono::steady_clock;
    Clock::time_point last_render_time{};
    bool rendered_once = false;

    const auto render_progress = [&](std::size_t written, std::size_t total) {
        const Clock::time_point now = Clock::now();
        if (rendered_once && now - last_render_time < std::chrono::milliseconds(100)) {
            return;
        }
        last_render_time = now;
        rendered_once = true;

        if (!canvas.BeginFrame(palette.bg)) {
            return;
        }
        const float screen_w = static_cast<float>(canvas.Width());
        const float screen_h = static_cast<float>(canvas.Height());
        canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
        canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});

        const float bar_w = screen_w * 0.5f;
        const float bar_h = 32.0f;
        const float bar_x = (screen_w - bar_w) * 0.5f;
        const float bar_y = (screen_h - bar_h) * 0.5f;

        const std::string label = Tr("updates.installing");
        const float label_width = MeasureText(atlas, canvas, label);
        DrawText(canvas, atlas, (screen_w - label_width) * 0.5f, bar_y - atlas.LineHeight() - 8.0f,
                label, palette.text);

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
    const DownloadResult result = DownloadAndInstallUpdate(info, render_progress);
    if (result != DownloadResult::Success) {
        model.notice_text = result == DownloadResult::ChecksumMismatch
                                 ? Tr("updates.install.checksum_mismatch")
                                 : Tr("updates.install.network_error");
        model.notice_is_error = true;
        return;
    }

    model.update_ready_to_close = true;
}

void DrawUpdateReadyModal(GpuCanvas& canvas, GlyphAtlas& atlas) {
    const UiPalette& palette = CurrentPalette();
    const float screen_w = static_cast<float>(canvas.Width());
    const float screen_h = static_cast<float>(canvas.Height());
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, palette.bg);
    canvas.DrawQuad(0.0f, 0.0f, screen_w, screen_h, CanvasColor{0.0f, 0.0f, 0.0f, 0.55f});

    constexpr float kPanelW = 620.0f;
    constexpr float kPanelPaddingX = 24.0f;
    constexpr float kTitleTop = 40.0f;
    constexpr float kBodyTop = 78.0f;
    constexpr float kBottomPadding = 32.0f;
    const float body_w = kPanelW - kPanelPaddingX * 2.0f;
    const std::vector<std::string> body_lines =
        WrapText(canvas, atlas, Tr("updates.install.ready_body"), body_w);
    const float panel_h =
        kBodyTop + static_cast<float>(body_lines.size()) * atlas.LineHeight() + kBottomPadding;

    const float panel_x = (screen_w - kPanelW) * 0.5f;
    const float panel_y = (screen_h - panel_h) * 0.5f;
    constexpr float kOutlineWidth = 2.0f;
    canvas.DrawQuad(panel_x - kOutlineWidth, panel_y - kOutlineWidth, kPanelW + kOutlineWidth * 2.0f,
                    panel_h + kOutlineWidth * 2.0f, palette.accent);
    canvas.DrawQuad(panel_x, panel_y, kPanelW, panel_h, palette.surface);

    DrawText(canvas, atlas, panel_x + kPanelPaddingX, panel_y + kTitleTop,
            Tr("updates.install.ready_title"), palette.accent);
    float line_y = panel_y + kBodyTop;
    for (const std::string& line : body_lines) {
        DrawText(canvas, atlas, panel_x + kPanelPaddingX, line_y, line, palette.text);
        line_y += atlas.LineHeight();
    }
}

} // namespace SwitchFrontend
