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

enum class SettingsTab { Graphics, Debug, Misc, Controls, Updates };
extern const std::array<std::pair<SettingsTab, const char*>, 5> kSettingsTabs;
inline constexpr int kNumSettingsTabs = 5;

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
    SettingRowDisableRightEye,
    SettingRowCpuClock,
    SettingRowNew3ds,
    SettingRowCpuJit,
    SettingRowRegion,
    SettingRowLanguage,
    SettingRowPointerSource,
    SettingRowGyroSensitivity,
    SettingRowPreloadTextures,
    SettingRowDumpTextures,
    SettingRowLayoutCycle,
    SettingRowDisablePipelineFastPath,
    SettingRowSkipSlowDraw,
    SettingRowSkipTextureCopy,
    SettingRowSkipCpuWrite,
    SettingRowEnableCompileBoost,
    SettingRowCustomTextures,
    // Not emitted by BuildSettingRows — a tag the in-game settings screen uses for its own
    // synthetic "Edit Screen Layout" action row, which has no MenuSettings field of its own (it
    // launches the layout editor instead; top/bottom screen opacity also lives there, not as a
    // Settings-tab row — see layout_editor.cpp). IsBooleanSetting/CycleSetting/IsPerGameEditable
    // all fall through to their default case for it, same as any other value BuildSettingRows
    // never produces.
    SettingRowEditLayout,
    SettingRowMovieThrottle,
    SettingRowPointerMode,
};

struct SettingRow {
    SettingRowIdx item;
    const char* label;
    std::string value;
    const char* description;
};

const char* RegionName(int region);
const char* LanguageName(int language);
const char* TextureFilterName(int filter);
std::string ResolutionText(int factor);
std::string LayoutCycleSummary(std::uint32_t mask);
std::string GyroSensitivityText(const MenuSettings& s);
std::string GyroSensitivityArmedText(const MenuSettings& s, bool y_axis);

std::vector<SettingRow> BuildSettingRows(SettingsTab tab, const MenuSettings& s);
void AdjustGyroAxis(MenuSettings& s, bool y_axis, int dir);

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

} // namespace SwitchFrontend
