// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "citra_switch/menu_data.h"

// The settings-row model: what a setting row is, how it's cycled/toggled, and what its display
// value reads as. This is the single source of truth shared by the library's Settings tab
// (menu.cpp) and the in-game settings screen — moved out of menu.cpp's anonymous namespace so
// both can drive the exact same rows/logic instead of each keeping its own copy.
namespace SwitchFrontend {

enum class SettingsTab { Display, Performance, Advanced, System, Paths, Controls, Updates };
extern const std::array<std::pair<SettingsTab, const char*>, 7> kSettingsTabs;
inline constexpr int kNumSettingsTabs = 7;

enum SettingRowIdx {
    SettingRowResolution,
    SettingRowVSync,
    SettingRowAsyncGpu,
    SettingRowStrictGpuSync,
    SettingRowAsyncShaders,
    SettingRowDiskShaderCache,
    SettingRowHwShader,
    SettingRowUbershaders,
    SettingRowTextureFilter,
    SettingRowLinearFiltering,
    SettingRowIntegerScaling,
    SettingRowShowFps,
    SettingRowShowShaderCompileProgress,
    SettingRowDisableRightEye,
    SettingRowCpuClock,
    SettingRowNew3ds,
    SettingRowPluginLoader,
    SettingRowAllowPluginLoader,
    SettingRowCpuJit,
    SettingRowFastmem,
    SettingRowRegion,
    SettingRowLanguage,
    SettingRowPointerSource,
    SettingRowGyroSensitivity,
    SettingRowPreloadTextures,
    SettingRowDumpTextures,
    SettingRowLayout,
    SettingRowLayoutCycle,
    SettingRowDisablePipelineFastPath,
    SettingRowSkipSlowDraw,
    SettingRowSkipTextureCopy,
    SettingRowSkipCpuWrite,
    SettingRowEnableCompileBoost,
    SettingRowEnableGpuFrameLog,
    SettingRowCustomTextures,
    // Not emitted by BuildSettingRows — a tag the in-game settings screen uses for its own
    // synthetic "Edit Screen Layout" action row, which has no MenuSettings field of its own (it
    // launches the layout editor instead; top/bottom screen opacity also lives there, not as a
    // Settings-tab row — see layout_editor.cpp). IsBooleanSetting/CycleSetting/IsPerGameEditable
    // all fall through to their default case for it, same as any other value BuildSettingRows
    // never produces.
    SettingRowEditLayout,
    SettingRowMovieThrottle,
    SettingRowMovieThrottleEnabled,
    SettingRowGameTdbEnabled,
    // Not a MenuSettings-backed value - Confirm on this row kicks off (or reopens the progress
    // popup for) gametdb.cpp's background cover download instead of toggling/cycling anything.
    // IsBooleanSetting/IsCyclableRow/etc. all fall through to their default case for it.
    SettingRowDownloadCovers,
    SettingRowPointerMode,
    // Non-selectable group divider - only `label` is meaningful (see SettingRow::is_header).
    SettingRowSectionHeader,
};

struct SettingRow {
    SettingRowIdx item;
    // std::string, not const char* - most rows still pass a literal (implicitly converts fine),
    // but a growing number pass a Tr() result (see ui_strings.h) for the ones already localized.
    std::string label;
    std::string value;
    std::string description;
    // True for a group-divider row (accent label + underline, same idiom as ui_controls.cpp's
    // ControlEntry::is_header) - non-selectable, skipped by FirstSelectableRow/NextSelectableRow.
    bool is_header = false;
};

std::string RegionName(int region);
const char* LanguageName(int language);
std::string TextureFilterName(int filter);
std::string ResolutionText(int factor);
std::string LayoutCycleSummary(std::uint32_t mask);
std::string GyroSensitivityText(const MenuSettings& s);
std::string GyroSensitivityArmedText(const MenuSettings& s, bool y_axis);

std::vector<SettingRow> BuildSettingRows(SettingsTab tab, const MenuSettings& s);
void AdjustGyroAxis(MenuSettings& s, bool y_axis, int dir);

// Every IsPerGameEditable row across the Graphics/Debug/Misc tabs, minus gyro sensitivity (its
// two-axis armed-bar interaction doesn't fit a plain toggle/cycle row model) - the single source
// of truth for both the library's per-game settings screen (ui_gamesettings.cpp) and the in-game
// quick menu (citra_switch.cpp's RunGame), so the two never drift out of sync with each other or
// with IsPerGameEditable's own filter.
std::vector<SettingRow> BuildPerGameSettingRows(const MenuSettings& s);

// Rows that cycle a value in place via the joystick once armed. Boolean rows are handled by
// IsBooleanSetting/ToggleSetting below instead (flipped directly by an A press).
void CycleSetting(MenuSettings& s, SettingRowIdx item, int dir);

// True for rows with only two states, toggled directly by an A press rather than armed for
// joystick adjustment.
bool IsBooleanSetting(SettingRowIdx item);
void ToggleSetting(MenuSettings& s, SettingRowIdx item);

// True for rows that have a per-game override mapping (see game_settings.h's OverrideField) and
// so are safe to expose in the in-game settings screen. Rows without one (Internal Resolution,
// VSync, boot-time-only Debug flags, Region/Language/New3DS, R3 layout cycle, …) only make sense
// as a global, pre-boot choice — editing them in-game would either silently leak into the global
// config for every other title or have no effect until next boot, so the in-game host filters
// them out of BuildSettingRows' result entirely rather than showing a dead-end row.
bool IsPerGameEditable(SettingRowIdx item);

bool RequiresRestart(SettingRowIdx item);

// Index of the first selectable (non-header) row, or 0 if `rows` has none (callers index a
// non-empty rows vector elsewhere, so this never needs to signal "no selectable row" separately).
int FirstSelectableRow(const std::vector<SettingRow>& rows);

// Moves `index` by `dir` (+1/-1), skipping over header rows; stays put if that runs off either
// end. Same idiom as ui_controls.cpp's local NextSelectable, shared here since three screens
// (library Settings, per-game Settings, in-game quick menu) all need it now.
int NextSelectableRow(const std::vector<SettingRow>& rows, int index, int dir);

} // namespace SwitchFrontend
