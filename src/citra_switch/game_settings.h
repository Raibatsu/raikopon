// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "citra_switch/menu_data.h"

// Per-game overrides for the handful of settings the in-game quick menu can edit. A game with
// no override file just follows the global config, exactly like Azahar's desktop per-game
// config works for Settings::SwitchableSetting — this is that same idea, minus the desktop UI,
// driven from the quick menu instead.
namespace SwitchFrontend {

// One field per quick-menu-editable setting. Unset means "follow the global value".
struct GameOverrides {
    std::optional<int> texture_filter;
    std::optional<bool> show_fps;
    std::optional<bool> custom_textures;
    std::optional<bool> disable_right_eye_render;
    std::optional<int> layout_option;
    std::optional<bool> swap_screen;
    std::optional<bool> upright_screen;
    std::optional<bool> upright_screen_flipped;
    std::optional<int> small_screen_position;
    std::optional<int> gyro_sensitivity_x;
    std::optional<int> gyro_sensitivity_y;
    std::optional<int> pointer_source;
    std::optional<int> movie_throttle_clock_percentage;
    std::optional<int> cpu_clock_percentage;
    std::optional<bool> enable_compile_boost;
    // The in-game settings screen additionally exposes these — everything the library's
    // Graphics/Debug/Misc tabs can edit except the boot-time-only / structurally-global fields
    // (custom textures, texture preload/dump, region/language/New3DS, CPU JIT, R3 layout cycle).
    std::optional<int> resolution_factor;
    std::optional<bool> use_vsync;
    std::optional<bool> async_shader_compilation;
    std::optional<bool> use_disk_shader_cache;
    std::optional<bool> use_hw_shader;
    std::optional<bool> filter_mode;
    std::optional<bool> use_integer_scaling;
    std::optional<bool> disable_pipeline_fast_path;
    std::optional<bool> skip_slow_draw;
    std::optional<bool> skip_texture_copy;
    std::optional<bool> skip_cpu_write;
    // Free-form rects and per-screen opacity for LayoutOption::CustomLayout, written by the
    // layout editor (not exposed as Settings-tab rows at all — see layout_editor.cpp). Carried by
    // OverrideField::ScreenLayout alongside the preset fields above.
    std::optional<int> custom_top_x;
    std::optional<int> custom_top_y;
    std::optional<int> custom_top_width;
    std::optional<int> custom_top_height;
    std::optional<int> custom_bottom_x;
    std::optional<int> custom_bottom_y;
    std::optional<int> custom_bottom_width;
    std::optional<int> custom_bottom_height;
    std::optional<int> top_screen_opacity;
    std::optional<int> bottom_screen_opacity;
};

// Which quick-menu action changed, so MarkGameOverride knows which field(s) to snapshot from
// the live state it just wrote to.
enum class OverrideField {
    TextureFilter,
    ShowFps,
    CustomTextures,
    RightEyeRender,
    ScreenLayout, // layout_option, swap_screen, upright_screen, upright_screen_flipped, small_screen_position together — a layout preset touches all five at once.
    GyroSensitivity, // X and Y together — Adjust() always changes just one, but both are always present once either is customized.
    PointerSource,
    MovieThrottleClock,
    CpuClock,
    EnableCompileBoost,
    Resolution,
    VSync,
    AsyncShaderCompilation,
    DiskShaderCache,
    HwShader,
    LinearFiltering,
    IntegerScaling,
    DisablePipelineFastPath,
    SkipSlowDraw,
    SkipTextureCopy,
    SkipCpuWrite,
};

// Loads <config dir>/game_settings/<TITLEID>.ini (if any) and pushes every field it has onto
// the live Settings::values / frontend state — SwitchableSetting-backed fields stop following
// global, frontend-only ones (gyro, pointer source, movie throttle) are just overwritten
// outright. Call once per boot, after the title's program_id is known.
void BeginGameOverrides(std::uint64_t program_id);

// Call BEFORE writing a field the quick menu is about to change. Switches that field's
// SwitchableSettings off global first, so SetValue() lands in the per-game `custom` slot rather
// than overwriting the global config — SwitchableSetting::SetValue writes whichever slot
// use_global currently selects, so a game with no override file yet would otherwise have every
// quick-menu edit leak into the global defaults for every other title. No-op with no game booted
// (leaving a setting stuck non-global with nothing to restore it) and for the frontend-only
// fields, which have no global/custom split at all.
void BeginFieldOverride(OverrideField field);

// Records that the quick menu just changed `field`, snapshotting its current live value(s)
// into the in-memory override set. Doesn't touch disk — see FlushGameOverrides.
void MarkGameOverride(OverrideField field);

// Writes the in-memory override set to disk if MarkGameOverride changed anything since the
// last flush. Safe to call even if nothing is dirty (no-op) or no game has booted (no-op).
void FlushGameOverrides();

// Diffs `before`/`after` (typically a MenuSettings snapshot taken at open vs. at close of the
// in-game settings screen) and, for each field that actually changed, runs the same
// BeginFieldOverride -> write -> MarkGameOverride sequence the quick menu used to do per
// keypress, then FlushGameOverrides(). Only fields with an OverrideField mapping are handled —
// see settings_model.h's IsPerGameEditable() for the corresponding UI-side row filter.
void CommitMenuSettingsPerGame(const MenuSettings& before, const MenuSettings& after);

// Discards every per-game override for the current title — reverts every SwitchableSetting this
// session took off global back to following it, restores the frontend-only fields to what they
// were before any override applied, clears the in-memory override set, and deletes the on-disk
// override file. The game keeps running and can still pick up new overrides afterward (unlike
// EndGameOverrides, this does not end the session). No-op if no game has booted.
void ResetGameOverridesToLibrary();

// Flushes any pending changes, then reverts every SwitchableSetting BeginGameOverrides touched
// back to following global, and restores the frontend-only fields (gyro sensitivity, pointer
// source, movie throttle) to whatever they were before this session's overrides applied. Call
// once the game has fully shut down.
void EndGameOverrides();

} // namespace SwitchFrontend
