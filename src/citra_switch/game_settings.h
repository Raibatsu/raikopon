// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

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
};

// Loads <config dir>/game_settings/<TITLEID>.ini (if any) and pushes every field it has onto
// the live Settings::values / frontend state — SwitchableSetting-backed fields stop following
// global, frontend-only ones (gyro, pointer source, movie throttle) are just overwritten
// outright. Call once per boot, after the title's program_id is known.
void BeginGameOverrides(std::uint64_t program_id);

// Records that the quick menu just changed `field`, snapshotting its current live value(s)
// into the in-memory override set. Doesn't touch disk — see FlushGameOverrides.
void MarkGameOverride(OverrideField field);

// Writes the in-memory override set to disk if MarkGameOverride changed anything since the
// last flush. Safe to call even if nothing is dirty (no-op) or no game has booted (no-op).
void FlushGameOverrides();

// Flushes any pending changes, then reverts every SwitchableSetting BeginGameOverrides touched
// back to following global, and restores the frontend-only fields (gyro sensitivity, pointer
// source, movie throttle) to whatever they were before this session's overrides applied. Call
// once the game has fully shut down.
void EndGameOverrides();

} // namespace SwitchFrontend
