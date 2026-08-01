// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/game_settings.h"

#include <algorithm>
#include <sstream>

#include <INIReader.h>
#include <fmt/format.h>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"

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
int s_pre_movie_throttle = 45;

// The custom-layout rects are plain Setting<u16>, not SwitchableSetting, so they need the same
// snapshot/restore treatment as the frontend-only fields above.
struct CustomRect {
    u16 top_x, top_y, top_w, top_h;
    u16 bottom_x, bottom_y, bottom_w, bottom_h;
};
CustomRect s_pre_custom_rect{};

CustomRect CurrentCustomRect() {
    const auto& v = Settings::values;
    return {v.custom_top_x.GetValue(),     v.custom_top_y.GetValue(),
            v.custom_top_width.GetValue(), v.custom_top_height.GetValue(),
            v.custom_bottom_x.GetValue(),  v.custom_bottom_y.GetValue(),
            v.custom_bottom_width.GetValue(), v.custom_bottom_height.GetValue()};
}

void ApplyCustomRect(const CustomRect& rect) {
    auto& v = Settings::values;
    v.custom_top_x = rect.top_x;
    v.custom_top_y = rect.top_y;
    v.custom_top_width = rect.top_w;
    v.custom_top_height = rect.top_h;
    v.custom_bottom_x = rect.bottom_x;
    v.custom_bottom_y = rect.bottom_y;
    v.custom_bottom_width = rect.bottom_w;
    v.custom_bottom_height = rect.bottom_h;
}

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
            static_cast<int>(ini.GetInteger(kSection, "movie_throttle_clock_percentage", 45));
    }
    if (has("cpu_clock_percentage")) {
        overrides.cpu_clock_percentage =
            static_cast<int>(ini.GetInteger(kSection, "cpu_clock_percentage", 100));
    }
    if (has("enable_compile_boost")) {
        overrides.enable_compile_boost = ini.GetBoolean(kSection, "enable_compile_boost", false);
    }
    if (has("resolution_factor")) {
        overrides.resolution_factor =
            static_cast<int>(ini.GetInteger(kSection, "resolution_factor", 1));
    }
    if (has("use_vsync")) {
        overrides.use_vsync = ini.GetBoolean(kSection, "use_vsync", false);
    }
    if (has("async_shader_compilation")) {
        overrides.async_shader_compilation =
            ini.GetBoolean(kSection, "async_shader_compilation", false);
    }
    if (has("use_disk_shader_cache")) {
        overrides.use_disk_shader_cache = ini.GetBoolean(kSection, "use_disk_shader_cache", true);
    }
    if (has("use_hw_shader")) {
        overrides.use_hw_shader = ini.GetBoolean(kSection, "use_hw_shader", true);
    }
    if (has("use_ubershaders")) {
        overrides.use_ubershaders = ini.GetBoolean(kSection, "use_ubershaders", true);
    }
    if (has("filter_mode")) {
        overrides.filter_mode = ini.GetBoolean(kSection, "filter_mode", true);
    }
    if (has("use_integer_scaling")) {
        overrides.use_integer_scaling = ini.GetBoolean(kSection, "use_integer_scaling", false);
    }
    if (has("disable_pipeline_fast_path")) {
        overrides.disable_pipeline_fast_path =
            ini.GetBoolean(kSection, "disable_pipeline_fast_path", false);
    }
    if (has("skip_slow_draw")) {
        overrides.skip_slow_draw = ini.GetBoolean(kSection, "skip_slow_draw", false);
    }
    if (has("skip_texture_copy")) {
        overrides.skip_texture_copy = ini.GetBoolean(kSection, "skip_texture_copy", false);
    }
    if (has("skip_cpu_write")) {
        overrides.skip_cpu_write = ini.GetBoolean(kSection, "skip_cpu_write", false);
    }
    if (has("top_screen_opacity")) {
        overrides.top_screen_opacity =
            static_cast<int>(ini.GetInteger(kSection, "top_screen_opacity", 100));
    }
    if (has("bottom_screen_opacity")) {
        overrides.bottom_screen_opacity =
            static_cast<int>(ini.GetInteger(kSection, "bottom_screen_opacity", 100));
    }
    const auto read_rect = [&](const char* key, std::optional<int>& field, int fallback) {
        if (has(key)) {
            field = static_cast<int>(ini.GetInteger(kSection, key, fallback));
        }
    };
    read_rect("custom_top_x", overrides.custom_top_x, 0);
    read_rect("custom_top_y", overrides.custom_top_y, 0);
    read_rect("custom_top_width", overrides.custom_top_width, 800);
    read_rect("custom_top_height", overrides.custom_top_height, 480);
    read_rect("custom_bottom_x", overrides.custom_bottom_x, 80);
    read_rect("custom_bottom_y", overrides.custom_bottom_y, 500);
    read_rect("custom_bottom_width", overrides.custom_bottom_width, 640);
    read_rect("custom_bottom_height", overrides.custom_bottom_height, 480);
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
                         overrides.movie_throttle_clock_percentage ||
                         overrides.cpu_clock_percentage || overrides.enable_compile_boost ||
                         overrides.resolution_factor || overrides.use_vsync ||
                         overrides.async_shader_compilation || overrides.use_disk_shader_cache ||
                         overrides.use_hw_shader || overrides.use_ubershaders ||
                         overrides.filter_mode ||
                         overrides.use_integer_scaling || overrides.disable_pipeline_fast_path ||
                         overrides.skip_slow_draw || overrides.skip_texture_copy ||
                         overrides.skip_cpu_write || overrides.top_screen_opacity ||
                         overrides.bottom_screen_opacity ||
                         overrides.custom_top_x || overrides.custom_top_y ||
                         overrides.custom_top_width || overrides.custom_top_height ||
                         overrides.custom_bottom_x || overrides.custom_bottom_y ||
                         overrides.custom_bottom_width || overrides.custom_bottom_height;
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
    write_int("cpu_clock_percentage", overrides.cpu_clock_percentage);
    write_bool("enable_compile_boost", overrides.enable_compile_boost);
    write_int("resolution_factor", overrides.resolution_factor);
    write_bool("use_vsync", overrides.use_vsync);
    write_bool("async_shader_compilation", overrides.async_shader_compilation);
    write_bool("use_disk_shader_cache", overrides.use_disk_shader_cache);
    write_bool("use_hw_shader", overrides.use_hw_shader);
    write_bool("use_ubershaders", overrides.use_ubershaders);
    write_bool("filter_mode", overrides.filter_mode);
    write_bool("use_integer_scaling", overrides.use_integer_scaling);
    write_bool("disable_pipeline_fast_path", overrides.disable_pipeline_fast_path);
    write_bool("skip_slow_draw", overrides.skip_slow_draw);
    write_bool("skip_texture_copy", overrides.skip_texture_copy);
    write_bool("skip_cpu_write", overrides.skip_cpu_write);
    write_int("top_screen_opacity", overrides.top_screen_opacity);
    write_int("bottom_screen_opacity", overrides.bottom_screen_opacity);
    write_int("custom_top_x", overrides.custom_top_x);
    write_int("custom_top_y", overrides.custom_top_y);
    write_int("custom_top_width", overrides.custom_top_width);
    write_int("custom_top_height", overrides.custom_top_height);
    write_int("custom_bottom_x", overrides.custom_bottom_x);
    write_int("custom_bottom_y", overrides.custom_bottom_y);
    write_int("custom_bottom_width", overrides.custom_bottom_width);
    write_int("custom_bottom_height", overrides.custom_bottom_height);

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
    // The custom rects are plain Settings, not SwitchableSetting, so there's no global/custom split
    // to switch — BeginGameOverrides snapshots them the same way it does the frontend-only fields.
    s_pre_custom_rect = CurrentCustomRect();
    const auto apply_rect = [](const std::optional<int>& field, Settings::Setting<u16>& setting) {
        if (field) {
            setting = static_cast<u16>(std::clamp(*field, 0, 0xFFFF));
        }
    };
    apply_rect(s_active.custom_top_x, v.custom_top_x);
    apply_rect(s_active.custom_top_y, v.custom_top_y);
    apply_rect(s_active.custom_top_width, v.custom_top_width);
    apply_rect(s_active.custom_top_height, v.custom_top_height);
    apply_rect(s_active.custom_bottom_x, v.custom_bottom_x);
    apply_rect(s_active.custom_bottom_y, v.custom_bottom_y);
    apply_rect(s_active.custom_bottom_width, v.custom_bottom_width);
    apply_rect(s_active.custom_bottom_height, v.custom_bottom_height);
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
    if (s_active.cpu_clock_percentage) {
        v.cpu_clock_percentage.SetGlobal(false);
        v.cpu_clock_percentage = *s_active.cpu_clock_percentage;
        // Running timers hold their own copy of the clock scale, so a value set while the core is
        // already up has to be pushed into core timing to take effect without a reboot.
        auto& system = Core::System::GetInstance();
        if (system.IsPoweredOn()) {
            system.CoreTiming().UpdateClockSpeed(
                static_cast<u32>(*s_active.cpu_clock_percentage));
        }
    }
    if (s_active.enable_compile_boost) {
        v.enable_compile_boost.SetGlobal(false);
        v.enable_compile_boost = *s_active.enable_compile_boost;
    }
    if (s_active.resolution_factor) {
        v.resolution_factor.SetGlobal(false);
        v.resolution_factor = static_cast<u32>(*s_active.resolution_factor);
    }
    if (s_active.use_vsync) {
        v.use_vsync.SetGlobal(false);
        v.use_vsync = *s_active.use_vsync;
    }
    if (s_active.async_shader_compilation) {
        v.async_shader_compilation.SetGlobal(false);
        v.async_shader_compilation = *s_active.async_shader_compilation;
    }
    if (s_active.use_disk_shader_cache) {
        v.use_disk_shader_cache.SetGlobal(false);
        v.use_disk_shader_cache = *s_active.use_disk_shader_cache;
    }
    if (s_active.use_hw_shader) {
        v.use_hw_shader.SetGlobal(false);
        v.use_hw_shader = *s_active.use_hw_shader;
    }
    if (s_active.use_ubershaders) {
        v.use_ubershaders.SetGlobal(false);
        v.use_ubershaders = *s_active.use_ubershaders;
    }
    if (s_active.filter_mode) {
        v.filter_mode.SetGlobal(false);
        v.filter_mode = *s_active.filter_mode;
    }
    if (s_active.use_integer_scaling) {
        v.use_integer_scaling.SetGlobal(false);
        v.use_integer_scaling = *s_active.use_integer_scaling;
    }
    if (s_active.disable_pipeline_fast_path) {
        v.disable_pipeline_fast_path.SetGlobal(false);
        v.disable_pipeline_fast_path = *s_active.disable_pipeline_fast_path;
    }
    if (s_active.skip_slow_draw) {
        v.skip_slow_draw.SetGlobal(false);
        v.skip_slow_draw = *s_active.skip_slow_draw;
    }
    if (s_active.skip_texture_copy) {
        v.skip_texture_copy.SetGlobal(false);
        v.skip_texture_copy = *s_active.skip_texture_copy;
    }
    if (s_active.skip_cpu_write) {
        v.skip_cpu_write.SetGlobal(false);
        v.skip_cpu_write = *s_active.skip_cpu_write;
    }
    if (s_active.top_screen_opacity) {
        v.top_screen_opacity.SetGlobal(false);
        v.top_screen_opacity = static_cast<u16>(*s_active.top_screen_opacity);
    }
    if (s_active.bottom_screen_opacity) {
        v.bottom_screen_opacity.SetGlobal(false);
        v.bottom_screen_opacity = static_cast<u16>(*s_active.bottom_screen_opacity);
    }
}

// Takes one setting off global, carrying its current effective value into the custom slot.
// `custom` is value-initialized (0/false), not seeded from the global value, so a bare
// SetGlobal(false) would make the setting read as zero until something wrote to it — which breaks
// any caller that computes its new value from the old one (the layout stepper, the clock stepper).
template <typename SettingT>
void TakeCustom(SettingT& setting) {
    if (!setting.UsingGlobal()) {
        return;
    }
    const auto current = setting.GetValue();
    setting.SetGlobal(false);
    setting.SetValue(current);
}

void BeginFieldOverride(OverrideField field) {
    if (s_program_id == 0) {
        return;
    }
    auto& v = Settings::values;
    switch (field) {
    case OverrideField::TextureFilter:
        TakeCustom(v.texture_filter);
        break;
    case OverrideField::ShowFps:
        TakeCustom(v.show_fps);
        break;
    case OverrideField::CustomTextures:
        TakeCustom(v.custom_textures);
        break;
    case OverrideField::RightEyeRender:
        TakeCustom(v.disable_right_eye_render);
        break;
    case OverrideField::ScreenLayout:
        TakeCustom(v.layout_option);
        TakeCustom(v.swap_screen);
        TakeCustom(v.upright_screen);
        TakeCustom(v.upright_screen_flipped);
        TakeCustom(v.small_screen_position);
        TakeCustom(v.top_screen_opacity);
        TakeCustom(v.bottom_screen_opacity);
        break;
    case OverrideField::CpuClock:
        TakeCustom(v.cpu_clock_percentage);
        break;
    case OverrideField::EnableCompileBoost:
        TakeCustom(v.enable_compile_boost);
        break;
    case OverrideField::Resolution:
        TakeCustom(v.resolution_factor);
        break;
    case OverrideField::VSync:
        TakeCustom(v.use_vsync);
        break;
    case OverrideField::AsyncShaderCompilation:
        TakeCustom(v.async_shader_compilation);
        break;
    case OverrideField::DiskShaderCache:
        TakeCustom(v.use_disk_shader_cache);
        break;
    case OverrideField::HwShader:
        TakeCustom(v.use_hw_shader);
        break;
    case OverrideField::Ubershaders:
        TakeCustom(v.use_ubershaders);
        break;
    case OverrideField::LinearFiltering:
        TakeCustom(v.filter_mode);
        break;
    case OverrideField::IntegerScaling:
        TakeCustom(v.use_integer_scaling);
        break;
    case OverrideField::DisablePipelineFastPath:
        TakeCustom(v.disable_pipeline_fast_path);
        break;
    case OverrideField::SkipSlowDraw:
        TakeCustom(v.skip_slow_draw);
        break;
    case OverrideField::SkipTextureCopy:
        TakeCustom(v.skip_texture_copy);
        break;
    case OverrideField::SkipCpuWrite:
        TakeCustom(v.skip_cpu_write);
        break;
    case OverrideField::GyroSensitivity:
    case OverrideField::PointerSource:
    case OverrideField::MovieThrottleClock:
        // Frontend-only state, no global/custom split to switch.
        break;
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
        s_active.custom_top_x = v.custom_top_x.GetValue();
        s_active.custom_top_y = v.custom_top_y.GetValue();
        s_active.custom_top_width = v.custom_top_width.GetValue();
        s_active.custom_top_height = v.custom_top_height.GetValue();
        s_active.custom_bottom_x = v.custom_bottom_x.GetValue();
        s_active.custom_bottom_y = v.custom_bottom_y.GetValue();
        s_active.custom_bottom_width = v.custom_bottom_width.GetValue();
        s_active.custom_bottom_height = v.custom_bottom_height.GetValue();
        s_active.top_screen_opacity = static_cast<int>(v.top_screen_opacity.GetValue());
        s_active.bottom_screen_opacity = static_cast<int>(v.bottom_screen_opacity.GetValue());
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
    case OverrideField::CpuClock:
        s_active.cpu_clock_percentage = static_cast<int>(v.cpu_clock_percentage.GetValue());
        break;
    case OverrideField::EnableCompileBoost:
        s_active.enable_compile_boost = v.enable_compile_boost.GetValue();
        break;
    case OverrideField::Resolution:
        s_active.resolution_factor = static_cast<int>(v.resolution_factor.GetValue());
        break;
    case OverrideField::VSync:
        s_active.use_vsync = v.use_vsync.GetValue();
        break;
    case OverrideField::AsyncShaderCompilation:
        s_active.async_shader_compilation = v.async_shader_compilation.GetValue();
        break;
    case OverrideField::DiskShaderCache:
        s_active.use_disk_shader_cache = v.use_disk_shader_cache.GetValue();
        break;
    case OverrideField::HwShader:
        s_active.use_hw_shader = v.use_hw_shader.GetValue();
        break;
    case OverrideField::Ubershaders:
        s_active.use_ubershaders = v.use_ubershaders.GetValue();
        break;
    case OverrideField::LinearFiltering:
        s_active.filter_mode = v.filter_mode.GetValue();
        break;
    case OverrideField::IntegerScaling:
        s_active.use_integer_scaling = v.use_integer_scaling.GetValue();
        break;
    case OverrideField::DisablePipelineFastPath:
        s_active.disable_pipeline_fast_path = v.disable_pipeline_fast_path.GetValue();
        break;
    case OverrideField::SkipSlowDraw:
        s_active.skip_slow_draw = v.skip_slow_draw.GetValue();
        break;
    case OverrideField::SkipTextureCopy:
        s_active.skip_texture_copy = v.skip_texture_copy.GetValue();
        break;
    case OverrideField::SkipCpuWrite:
        s_active.skip_cpu_write = v.skip_cpu_write.GetValue();
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

void CommitMenuSettingsPerGame(const MenuSettings& before, const MenuSettings& after) {
    auto& v = Settings::values;
    if (after.texture_filter != before.texture_filter) {
        BeginFieldOverride(OverrideField::TextureFilter);
        v.texture_filter =
            static_cast<Settings::TextureFilter>(std::clamp(after.texture_filter, 0, 5));
        MarkGameOverride(OverrideField::TextureFilter);
    }
    if (after.show_fps != before.show_fps) {
        BeginFieldOverride(OverrideField::ShowFps);
        v.show_fps = after.show_fps;
        MarkGameOverride(OverrideField::ShowFps);
    }
    if (after.custom_textures != before.custom_textures) {
        BeginFieldOverride(OverrideField::CustomTextures);
        v.custom_textures = after.custom_textures;
        MarkGameOverride(OverrideField::CustomTextures);
    }
    if (after.disable_right_eye_render != before.disable_right_eye_render) {
        BeginFieldOverride(OverrideField::RightEyeRender);
        v.disable_right_eye_render = after.disable_right_eye_render;
        MarkGameOverride(OverrideField::RightEyeRender);
    }
    if (after.gyro_sensitivity_x != before.gyro_sensitivity_x ||
        after.gyro_sensitivity_y != before.gyro_sensitivity_y) {
        SetGyroSensitivity(after.gyro_sensitivity_x, after.gyro_sensitivity_y);
        MarkGameOverride(OverrideField::GyroSensitivity);
    }
    if (after.pointer_source != before.pointer_source) {
        SetPointerSource(static_cast<PointerSource>(
            std::clamp(after.pointer_source, 0, NumPointerSources - 1)));
        MarkGameOverride(OverrideField::PointerSource);
    }
    if (after.cpu_clock_percentage != before.cpu_clock_percentage) {
        BeginFieldOverride(OverrideField::CpuClock);
        // The running timers keep their own copy of the clock scale, so the change has to be
        // pushed into core timing to take effect without a reboot (mirrors what the old quick
        // menu's SetCpuClock() did).
        v.cpu_clock_percentage = std::clamp(after.cpu_clock_percentage, 25, 400);
        auto& system = Core::System::GetInstance();
        if (system.IsPoweredOn()) {
            system.CoreTiming().UpdateClockSpeed(
                static_cast<u32>(v.cpu_clock_percentage.GetValue()));
        }
        MarkGameOverride(OverrideField::CpuClock);
    }
    if (after.enable_compile_boost != before.enable_compile_boost) {
        BeginFieldOverride(OverrideField::EnableCompileBoost);
        v.enable_compile_boost = after.enable_compile_boost;
        MarkGameOverride(OverrideField::EnableCompileBoost);
    }
    if (after.resolution_factor != before.resolution_factor) {
        BeginFieldOverride(OverrideField::Resolution);
        v.resolution_factor = static_cast<u32>(std::clamp(after.resolution_factor, 0, 10));
        MarkGameOverride(OverrideField::Resolution);
    }
    if (after.use_vsync != before.use_vsync) {
        BeginFieldOverride(OverrideField::VSync);
        v.use_vsync = after.use_vsync;
        MarkGameOverride(OverrideField::VSync);
    }
    if (after.async_shader_compilation != before.async_shader_compilation) {
        BeginFieldOverride(OverrideField::AsyncShaderCompilation);
        v.async_shader_compilation = after.async_shader_compilation;
        MarkGameOverride(OverrideField::AsyncShaderCompilation);
    }
    if (after.use_disk_shader_cache != before.use_disk_shader_cache) {
        BeginFieldOverride(OverrideField::DiskShaderCache);
        v.use_disk_shader_cache = after.use_disk_shader_cache;
        MarkGameOverride(OverrideField::DiskShaderCache);
    }
    if (after.use_hw_shader != before.use_hw_shader) {
        BeginFieldOverride(OverrideField::HwShader);
        v.use_hw_shader = after.use_hw_shader;
        MarkGameOverride(OverrideField::HwShader);
    }
    if (after.use_ubershaders != before.use_ubershaders) {
        BeginFieldOverride(OverrideField::Ubershaders);
        v.use_ubershaders = after.use_ubershaders;
        MarkGameOverride(OverrideField::Ubershaders);
    }
    if (after.filter_mode != before.filter_mode) {
        BeginFieldOverride(OverrideField::LinearFiltering);
        v.filter_mode = after.filter_mode;
        MarkGameOverride(OverrideField::LinearFiltering);
    }
    if (after.use_integer_scaling != before.use_integer_scaling) {
        BeginFieldOverride(OverrideField::IntegerScaling);
        v.use_integer_scaling = after.use_integer_scaling;
        MarkGameOverride(OverrideField::IntegerScaling);
    }
    if (after.disable_pipeline_fast_path != before.disable_pipeline_fast_path) {
        BeginFieldOverride(OverrideField::DisablePipelineFastPath);
        v.disable_pipeline_fast_path = after.disable_pipeline_fast_path;
        MarkGameOverride(OverrideField::DisablePipelineFastPath);
    }
    if (after.skip_slow_draw != before.skip_slow_draw) {
        BeginFieldOverride(OverrideField::SkipSlowDraw);
        v.skip_slow_draw = after.skip_slow_draw;
        MarkGameOverride(OverrideField::SkipSlowDraw);
    }
    if (after.skip_texture_copy != before.skip_texture_copy) {
        BeginFieldOverride(OverrideField::SkipTextureCopy);
        v.skip_texture_copy = after.skip_texture_copy;
        MarkGameOverride(OverrideField::SkipTextureCopy);
    }
    if (after.skip_cpu_write != before.skip_cpu_write) {
        BeginFieldOverride(OverrideField::SkipCpuWrite);
        v.skip_cpu_write = after.skip_cpu_write;
        MarkGameOverride(OverrideField::SkipCpuWrite);
    }
    FlushGameOverrides();
}

void ResetGameOverridesToLibrary() {
    if (s_program_id == 0) {
        return;
    }
    // SwitchableSetting-backed fields: switching back to global is enough, because
    // BeginFieldOverride took each one off global before anything wrote to it, so every edit
    // landed in `custom` and the global slot still holds what the config file loaded.
    Settings::RestoreGlobalState(false);

    // Frontend-only fields have no such split, so put back what BeginGameOverrides snapshotted.
    SetGyroSensitivity(s_pre_gyro_x, s_pre_gyro_y);
    SetPointerSource(static_cast<PointerSource>(s_pre_pointer_source));
    SetMovieThrottleClockPercentage(s_pre_movie_throttle);
    ApplyCustomRect(s_pre_custom_rect);

    s_active = {};
    s_dirty = true;
    FlushGameOverrides(); // writes the now-empty override set, i.e. deletes the file.
}

void EndGameOverrides() {
    FlushGameOverrides();
    if (s_program_id == 0) {
        return;
    }

    // SwitchableSetting-backed fields: switching back to global is enough, because
    // BeginFieldOverride took each one off global before the quick menu wrote to it, so every
    // edit landed in `custom` and the global slot still holds what the config file loaded.
    Settings::RestoreGlobalState(false);

    // Frontend-only fields have no such split, so put back what BeginGameOverrides snapshotted.
    SetGyroSensitivity(s_pre_gyro_x, s_pre_gyro_y);
    SetPointerSource(static_cast<PointerSource>(s_pre_pointer_source));
    SetMovieThrottleClockPercentage(s_pre_movie_throttle);
    ApplyCustomRect(s_pre_custom_rect);

    s_program_id = 0;
    s_active = {};
}

} // namespace SwitchFrontend
