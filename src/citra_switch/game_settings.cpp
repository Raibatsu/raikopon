// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/game_settings.h"

#include <sstream>

#include <INIReader.h>
#include <fmt/format.h>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"

namespace SwitchFrontend {

namespace {

std::string GameSettingsDir() {
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "game_settings" + DIR_SEP;
}

std::string GameSettingsFile(std::uint64_t program_id) {
    return GameSettingsDir() + fmt::format("{:016X}", program_id) + ".ini";
}

// The title this session's overrides belong to. 0 means no game has booted (or none had a
// program_id worth keying a file on), in which case every function below is a no-op.
std::uint64_t s_program_id = 0;
GameOverrides s_active{};
bool s_dirty = false;

// What the frontend-only fields (not real Settings::SwitchableSetting) held before
// BeginGameOverrides applied any override to them, so EndGameOverrides can put them back
// without needing its own global/custom split for each.
int s_pre_gyro_x = 100;
int s_pre_gyro_y = 100;
int s_pre_pointer_source = 0;
int s_pre_movie_throttle = 25;

GameOverrides ReadOverridesFile(std::uint64_t program_id) {
    GameOverrides overrides{};
    const std::string path = GameSettingsFile(program_id);
    if (!FileUtil::Exists(path)) {
        return overrides;
    }
    std::string buffer;
    if (!FileUtil::ReadFileToString(true, path, buffer)) {
        return overrides;
    }
    INIReader ini{buffer.c_str(), buffer.size()};
    if (ini.ParseError() < 0) {
        LOG_ERROR(Config, "Malformed per-game settings file '{}'", path);
        return overrides;
    }
    static constexpr const char* kSection = "Overrides";
    const auto has = [&](const char* key) { return !ini.Get(kSection, key, "").empty(); };
    if (has("texture_filter")) {
        overrides.texture_filter = static_cast<int>(ini.GetInteger(kSection, "texture_filter", 0));
    }
    if (has("show_fps")) {
        overrides.show_fps = ini.GetBoolean(kSection, "show_fps", false);
    }
    if (has("custom_textures")) {
        overrides.custom_textures = ini.GetBoolean(kSection, "custom_textures", false);
    }
    if (has("disable_right_eye_render")) {
        overrides.disable_right_eye_render = ini.GetBoolean(kSection, "disable_right_eye_render", false);
    }
    if (has("layout_option")) {
        overrides.layout_option = static_cast<int>(ini.GetInteger(kSection, "layout_option", 0));
    }
    if (has("swap_screen")) {
        overrides.swap_screen = ini.GetBoolean(kSection, "swap_screen", false);
    }
    if (has("upright_screen")) {
        overrides.upright_screen = ini.GetBoolean(kSection, "upright_screen", false);
    }
    if (has("upright_screen_flipped")) {
        overrides.upright_screen_flipped = ini.GetBoolean(kSection, "upright_screen_flipped", false);
    }
    if (has("small_screen_position")) {
        overrides.small_screen_position =
            static_cast<int>(ini.GetInteger(kSection, "small_screen_position", 0));
    }
    if (has("gyro_sensitivity_x")) {
        overrides.gyro_sensitivity_x =
            static_cast<int>(ini.GetInteger(kSection, "gyro_sensitivity_x", 100));
    }
    if (has("gyro_sensitivity_y")) {
        overrides.gyro_sensitivity_y =
            static_cast<int>(ini.GetInteger(kSection, "gyro_sensitivity_y", 100));
    }
    if (has("pointer_source")) {
        overrides.pointer_source = static_cast<int>(ini.GetInteger(kSection, "pointer_source", 0));
    }
    if (has("movie_throttle_clock_percentage")) {
        overrides.movie_throttle_clock_percentage =
            static_cast<int>(ini.GetInteger(kSection, "movie_throttle_clock_percentage", 25));
    }
    return overrides;
}

void WriteOverridesFile(std::uint64_t program_id, const GameOverrides& overrides) {
    const std::string path = GameSettingsFile(program_id);
    const bool any_set = overrides.texture_filter || overrides.show_fps ||
                         overrides.custom_textures || overrides.disable_right_eye_render ||
                         overrides.layout_option || overrides.swap_screen ||
                         overrides.upright_screen || overrides.upright_screen_flipped ||
                         overrides.small_screen_position || overrides.gyro_sensitivity_x ||
                         overrides.gyro_sensitivity_y || overrides.pointer_source ||
                         overrides.movie_throttle_clock_percentage;
    if (!any_set) {
        // Nothing customised (any longer) — no point leaving an empty file behind.
        FileUtil::Delete(path);
        return;
    }

    std::ostringstream ss;
    ss << "[Overrides]\n";
    const auto write_bool = [&](const char* key, const std::optional<bool>& v) {
        if (v) {
            ss << key << " = " << (*v ? "true" : "false") << '\n';
        }
    };
    const auto write_int = [&](const char* key, const std::optional<int>& v) {
        if (v) {
            ss << key << " = " << *v << '\n';
        }
    };
    write_int("texture_filter", overrides.texture_filter);
    write_bool("show_fps", overrides.show_fps);
    write_bool("custom_textures", overrides.custom_textures);
    write_bool("disable_right_eye_render", overrides.disable_right_eye_render);
    write_int("layout_option", overrides.layout_option);
    write_bool("swap_screen", overrides.swap_screen);
    write_bool("upright_screen", overrides.upright_screen);
    write_bool("upright_screen_flipped", overrides.upright_screen_flipped);
    write_int("small_screen_position", overrides.small_screen_position);
    write_int("gyro_sensitivity_x", overrides.gyro_sensitivity_x);
    write_int("gyro_sensitivity_y", overrides.gyro_sensitivity_y);
    write_int("pointer_source", overrides.pointer_source);
    write_int("movie_throttle_clock_percentage", overrides.movie_throttle_clock_percentage);

    FileUtil::CreateFullPath(path);
    if (!FileUtil::WriteStringToFile(true, path, ss.str())) {
        LOG_ERROR(Config, "Failed to save per-game settings to '{}'", path);
    }
}

} // namespace

void BeginGameOverrides(std::uint64_t program_id) {
    s_program_id = program_id;
    s_active = {};
    s_dirty = false;
    if (program_id == 0) {
        return;
    }

    // Snapshot the frontend-only fields' global values before anything below can overwrite them.
    s_pre_gyro_x = GetGyroSensitivityX();
    s_pre_gyro_y = GetGyroSensitivityY();
    s_pre_pointer_source = static_cast<int>(GetPointerSource());
    s_pre_movie_throttle = GetMovieThrottleClockPercentage();

    s_active = ReadOverridesFile(program_id);

    auto& v = Settings::values;
    if (s_active.texture_filter) {
        v.texture_filter.SetGlobal(false);
        v.texture_filter = static_cast<Settings::TextureFilter>(*s_active.texture_filter);
    }
    if (s_active.show_fps) {
        v.show_fps.SetGlobal(false);
        v.show_fps = *s_active.show_fps;
    }
    if (s_active.custom_textures) {
        v.custom_textures.SetGlobal(false);
        v.custom_textures = *s_active.custom_textures;
    }
    if (s_active.disable_right_eye_render) {
        v.disable_right_eye_render.SetGlobal(false);
        v.disable_right_eye_render = *s_active.disable_right_eye_render;
    }
    if (s_active.layout_option) {
        v.layout_option.SetGlobal(false);
        v.layout_option = static_cast<Settings::LayoutOption>(*s_active.layout_option);
    }
    if (s_active.swap_screen) {
        v.swap_screen.SetGlobal(false);
        v.swap_screen = *s_active.swap_screen;
    }
    if (s_active.upright_screen) {
        v.upright_screen.SetGlobal(false);
        v.upright_screen = *s_active.upright_screen;
    }
    if (s_active.upright_screen_flipped) {
        v.upright_screen_flipped.SetGlobal(false);
        v.upright_screen_flipped = *s_active.upright_screen_flipped;
    }
    if (s_active.small_screen_position) {
        v.small_screen_position.SetGlobal(false);
        v.small_screen_position =
            static_cast<Settings::SmallScreenPosition>(*s_active.small_screen_position);
    }
    if (s_active.gyro_sensitivity_x || s_active.gyro_sensitivity_y) {
        SetGyroSensitivity(s_active.gyro_sensitivity_x.value_or(s_pre_gyro_x),
                           s_active.gyro_sensitivity_y.value_or(s_pre_gyro_y));
    }
    if (s_active.pointer_source) {
        SetPointerSource(static_cast<PointerSource>(*s_active.pointer_source));
    }
    if (s_active.movie_throttle_clock_percentage) {
        SetMovieThrottleClockPercentage(*s_active.movie_throttle_clock_percentage);
    }
}

void MarkGameOverride(OverrideField field) {
    if (s_program_id == 0) {
        return;
    }
    const auto& v = Settings::values;
    switch (field) {
    case OverrideField::TextureFilter:
        s_active.texture_filter = static_cast<int>(v.texture_filter.GetValue());
        break;
    case OverrideField::ShowFps:
        s_active.show_fps = v.show_fps.GetValue();
        break;
    case OverrideField::CustomTextures:
        s_active.custom_textures = v.custom_textures.GetValue();
        break;
    case OverrideField::RightEyeRender:
        s_active.disable_right_eye_render = v.disable_right_eye_render.GetValue();
        break;
    case OverrideField::ScreenLayout:
        s_active.layout_option = static_cast<int>(v.layout_option.GetValue());
        s_active.swap_screen = v.swap_screen.GetValue();
        s_active.upright_screen = v.upright_screen.GetValue();
        s_active.upright_screen_flipped = v.upright_screen_flipped.GetValue();
        s_active.small_screen_position = static_cast<int>(v.small_screen_position.GetValue());
        break;
    case OverrideField::GyroSensitivity:
        s_active.gyro_sensitivity_x = GetGyroSensitivityX();
        s_active.gyro_sensitivity_y = GetGyroSensitivityY();
        break;
    case OverrideField::PointerSource:
        s_active.pointer_source = static_cast<int>(GetPointerSource());
        break;
    case OverrideField::MovieThrottleClock:
        s_active.movie_throttle_clock_percentage = GetMovieThrottleClockPercentage();
        break;
    }
    s_dirty = true;
}

void FlushGameOverrides() {
    if (s_program_id == 0 || !s_dirty) {
        return;
    }
    WriteOverridesFile(s_program_id, s_active);
    s_dirty = false;
}

void EndGameOverrides() {
    FlushGameOverrides();
    if (s_program_id == 0) {
        return;
    }

    // SwitchableSetting-backed fields: switching back to global is enough, the global slot was
    // never touched by SetValue() above.
    Settings::RestoreGlobalState(false);

    // Frontend-only fields have no such split, so put back what BeginGameOverrides snapshotted.
    SetGyroSensitivity(s_pre_gyro_x, s_pre_gyro_y);
    SetPointerSource(static_cast<PointerSource>(s_pre_pointer_source));
    SetMovieThrottleClockPercentage(s_pre_movie_throttle);

    s_program_id = 0;
    s_active = {};
}

} // namespace SwitchFrontend
