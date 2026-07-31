// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/settings_model.h"

namespace SwitchFrontend {

const std::array<std::pair<SettingsTab, const char*>, 5> kSettingsTabs{{
    {SettingsTab::Graphics, "Graphics"},
    {SettingsTab::Debug, "Debug"},
    {SettingsTab::Misc, "Misc"},
    {SettingsTab::Controls, "Controls"},
    {SettingsTab::Updates, "Updates"},
}};

const char* RegionName(int region) {
    switch (region) {
    case -1:
        return "Auto";
    case 0:
        return "Japan";
    case 1:
        return "USA";
    case 2:
        return "Europe";
    case 3:
        return "Australia";
    case 4:
        return "China";
    case 5:
        return "Korea";
    case 6:
        return "Taiwan";
    default:
        return "Auto";
    }
}

// Ordered to match Service::CFG::SystemLanguage.
// The overlay font is Latin-only, so names are spelled out in English only.
const char* LanguageName(int language) {
    switch (language) {
    case 0:
        return "Japanese";
    case 1:
        return "English";
    case 2:
        return "French";
    case 3:
        return "German";
    case 4:
        return "Italian";
    case 5:
        return "Spanish";
    case 6:
        return "Simplified Chinese";
    case 7:
        return "Korean";
    case 8:
        return "Dutch";
    case 9:
        return "Portuguese";
    case 10:
        return "Russian";
    case 11:
        return "Traditional Chinese";
    default:
        return "English";
    }
}

const char* TextureFilterName(int filter) {
    switch (filter) {
    case 0:
        return "None";
    case 1:
        return "Anime4K";
    case 2:
        return "Bicubic";
    case 3:
        return "ScaleForce";
    case 4:
        return "xBRZ";
    case 5:
        return "MMPX";
    default:
        return "None";
    }
}

std::string ResolutionText(int factor) {
    if (factor == 0) {
        return "Auto (window)";
    }
    if (factor == 1) {
        return "Native (1x)";
    }
    return std::to_string(factor) + "x";
}

// "N of M" summary of how many layouts R3 is set to cycle through.
std::string LayoutCycleSummary(std::uint32_t mask) {
    const int total = GetScreenLayoutCount();
    int enabled = 0;
    for (int i = 0; i < total; ++i) {
        if ((mask & (1u << i)) != 0) {
            ++enabled;
        }
    }
    return std::to_string(enabled) + " of " + std::to_string(total);
}

std::string GyroSensitivityText(const MenuSettings& s) {
    return "X " + std::to_string(s.gyro_sensitivity_x) + "%   Y " +
           std::to_string(s.gyro_sensitivity_y) + "%";
}

std::string GyroSensitivityArmedText(const MenuSettings& s, bool y_axis) {
    const std::string x = "X " + std::to_string(s.gyro_sensitivity_x) + "%";
    const std::string y = "Y " + std::to_string(s.gyro_sensitivity_y) + "%";
    return y_axis ? (x + "   [" + y + "]") : ("[" + x + "]   " + y);
}

std::vector<SettingRow> BuildSettingRows(SettingsTab tab, const MenuSettings& s) {
    switch (tab) {
    case SettingsTab::Graphics:
        return {
            {SettingRowResolution, "Internal Resolution", ResolutionText(s.resolution_factor),
             "Change the resolution the game is played at. 1x is 400x240."},
            {SettingRowVSync, "VSync", s.use_vsync ? "On" : "Off",
             "Reduces screen tearing at the cost of increased input latency."},
            {SettingRowAsyncGpu, "Async GPU (needs restart)", s.async_gpu_emulation ? "On" : "Off",
             "Runs GPU command processing on its own thread in parallel with the CPU. Faster, "
             "but takes effect on next launch."},
            {SettingRowStrictGpuSync, "Strict GPU Sync", s.strict_gpu_sync ? "On" : "Off",
             "Waits for the GPU thread to catch up every frame instead of letting it lag behind. "
             "Only matters with Async GPU on."},
            {SettingRowTextureFilter, "Texture Filter", TextureFilterName(s.texture_filter),
             "Add filters to your screen."},
            {SettingRowLinearFiltering, "Linear Filtering", s.filter_mode ? "On" : "Off",
             "Smooth out jagged edges at the cost of sharpness."},
            {SettingRowIntegerScaling, "Integer Scaling", s.use_integer_scaling ? "On" : "Off",
             "Scales the output 1:1 to the resolution of the game."},
        };
    case SettingsTab::Debug:
        return {
            {SettingRowAsyncShaders, "Async Shader Compilation",
             s.async_shader_compilation ? "On" : "Off",
             "Reduces the amount of time it takes to compile shaders."},
            {SettingRowDiskShaderCache, "Disk Shader Cache", s.use_disk_shader_cache ? "On" : "Off",
             "Drastically reduces stuttering in-game by keeping compiled shaders on disk."},
            {SettingRowHwShader, "Hardware Shader", s.use_hw_shader ? "On" : "Off",
             "Emulate shaders more efficiently on GPU."},
            {SettingRowEnableCompileBoost, "Enable Compile Boost",
             s.enable_compile_boost ? "On" : "Off",
             "Dramatically speeds up shader compiling speed but reduces GPU performance during "
             "that duration."},
            {SettingRowDisableRightEye, "Disable Right Eye Render",
             s.disable_right_eye_render ? "On" : "Off",
             "Disable this for huge speed boost. Only enable this if you have issues rendering "
             "the game on the bottom screen."},
            {SettingRowCpuClock, "CPU Clock", std::to_string(s.cpu_clock_percentage) + "%",
             "Change the emulated CPU clock. Most games play well at 100%."},
            {SettingRowCpuJit, "CPU JIT (dynarmic)", s.use_cpu_jit ? "On" : "Off",
             "Do not disable this unless explicitly needed. Huge performance drops."},
            {SettingRowPointerSource, "Touch Pointer Source",
             PointerSourceName(static_cast<PointerSource>(s.pointer_source)),
             "For those who don't want to use touch screen, use a virtual cursor."},
            {SettingRowGyroSensitivity, "Gyro Sensitivity", GyroSensitivityText(s),
             "Gyroscope Sensitivity."},
            {SettingRowCustomTextures, "Custom Textures", s.custom_textures ? "On" : "Off",
             "Enable loading a custom texture pack for this game."},
            {SettingRowPreloadTextures, "Preload Custom Textures",
             s.preload_textures ? "On" : "Off",
             "Enable use of custom textures in-game. Place them in "
             "/load/textures/<TITLE_ID>/."},
            {SettingRowDumpTextures, "Dump Textures", s.dump_textures ? "On" : "Off",
             "Disable this unless you're making your own texture pack. Heavy on IO calls."},
            {SettingRowDisablePipelineFastPath, "Disable Pipeline Fast Path",
             s.disable_pipeline_fast_path ? "On" : "Off",
             "Debug option that helps speed up shader compilation."},
            {SettingRowSkipSlowDraw, "Skip Slow Draw", s.skip_slow_draw ? "On" : "Off",
             "Debug option that skips the CPU vertex/triangle fallback entirely."},
            {SettingRowSkipTextureCopy, "Skip Texture Copy", s.skip_texture_copy ? "On" : "Off",
             "Debug option that skips non-cached source surface."},
            {SettingRowSkipCpuWrite, "Skip CPU Write", s.skip_cpu_write ? "On" : "Off",
             "Debug option that skips flushing/removing cached GPU surfaces for CPU cycles."},
        };
    case SettingsTab::Misc:
        return {
            {SettingRowShowFps, "Show FPS Counter", s.show_fps ? "On" : "Off",
             "Shows FPS on the top left of the screen."},
            {SettingRowNew3ds, "New 3DS Mode", s.is_new_3ds ? "On" : "Off",
             "Emulate New 3DS. Might cause performance drops but necessary to boot some games."},
            {SettingRowRegion, "Console Region", RegionName(s.region_value),
             "Change the region of the console."},
            {SettingRowLanguage, "System Language", LanguageName(s.language),
             "Change the language of games. Not supported in GUI for now."},
            {SettingRowLayoutCycle, "R3 Screen Layouts", LayoutCycleSummary(s.layout_cycle_mask),
             "Screen layouts to swap between when tapping screen-swap key."},
        };
    case SettingsTab::Controls:
    case SettingsTab::Updates:
        return {};
    }
    return {};
}

void AdjustGyroAxis(MenuSettings& s, bool y_axis, int dir) {
    int& v = y_axis ? s.gyro_sensitivity_y : s.gyro_sensitivity_x;
    v = std::clamp(v + dir * 10, 10, 500);
}

void CycleSetting(MenuSettings& s, SettingRowIdx item, int dir) {
    switch (item) {
    case SettingRowResolution:
        s.resolution_factor = std::clamp(s.resolution_factor + dir, 0, 4);
        break;
    case SettingRowTextureFilter:
        s.texture_filter = std::clamp(s.texture_filter + dir, 0, 5);
        break;
    case SettingRowCpuClock:
        // 1% steps like the movie throttle row, so hold-to-repeat scrubs it like a slider.
        s.cpu_clock_percentage = std::clamp(s.cpu_clock_percentage + dir, 25, 400);
        break;
    case SettingRowRegion:
        s.region_value = std::clamp(s.region_value + dir, -1, 6);
        break;
    case SettingRowLanguage:
        s.language = std::clamp(s.language + dir, 0, 11);
        break;
    case SettingRowPointerSource:
        s.pointer_source = std::clamp(s.pointer_source + dir, 0, NumPointerSources - 1);
        break;
    default:
        break;
    }
}

bool IsBooleanSetting(SettingRowIdx item) {
    switch (item) {
    case SettingRowVSync:
    case SettingRowAsyncGpu:
    case SettingRowStrictGpuSync:
    case SettingRowAsyncShaders:
    case SettingRowDiskShaderCache:
    case SettingRowHwShader:
    case SettingRowLinearFiltering:
    case SettingRowIntegerScaling:
    case SettingRowShowFps:
    case SettingRowDisableRightEye:
    case SettingRowNew3ds:
    case SettingRowCpuJit:
    case SettingRowPreloadTextures:
    case SettingRowDumpTextures:
    case SettingRowDisablePipelineFastPath:
    case SettingRowSkipSlowDraw:
    case SettingRowSkipTextureCopy:
    case SettingRowSkipCpuWrite:
    case SettingRowEnableCompileBoost:
    case SettingRowCustomTextures:
        return true;
    default:
        return false;
    }
}

void ToggleSetting(MenuSettings& s, SettingRowIdx item) {
    switch (item) {
    case SettingRowVSync:
        s.use_vsync = !s.use_vsync;
        break;
    case SettingRowAsyncGpu:
        s.async_gpu_emulation = !s.async_gpu_emulation;
        break;
    case SettingRowStrictGpuSync:
        s.strict_gpu_sync = !s.strict_gpu_sync;
        break;
    case SettingRowAsyncShaders:
        s.async_shader_compilation = !s.async_shader_compilation;
        break;
    case SettingRowDiskShaderCache:
        s.use_disk_shader_cache = !s.use_disk_shader_cache;
        break;
    case SettingRowHwShader:
        s.use_hw_shader = !s.use_hw_shader;
        break;
    case SettingRowLinearFiltering:
        s.filter_mode = !s.filter_mode;
        break;
    case SettingRowIntegerScaling:
        s.use_integer_scaling = !s.use_integer_scaling;
        break;
    case SettingRowShowFps:
        s.show_fps = !s.show_fps;
        break;
    case SettingRowDisableRightEye:
        s.disable_right_eye_render = !s.disable_right_eye_render;
        break;
    case SettingRowNew3ds:
        s.is_new_3ds = !s.is_new_3ds;
        break;
    case SettingRowCpuJit:
        s.use_cpu_jit = !s.use_cpu_jit;
        break;
    case SettingRowPreloadTextures:
        s.preload_textures = !s.preload_textures;
        break;
    case SettingRowDumpTextures:
        s.dump_textures = !s.dump_textures;
        break;
    case SettingRowDisablePipelineFastPath:
        s.disable_pipeline_fast_path = !s.disable_pipeline_fast_path;
        break;
    case SettingRowSkipSlowDraw:
        s.skip_slow_draw = !s.skip_slow_draw;
        break;
    case SettingRowSkipTextureCopy:
        s.skip_texture_copy = !s.skip_texture_copy;
        break;
    case SettingRowSkipCpuWrite:
        s.skip_cpu_write = !s.skip_cpu_write;
        break;
    case SettingRowEnableCompileBoost:
        s.enable_compile_boost = !s.enable_compile_boost;
        break;
    case SettingRowCustomTextures:
        s.custom_textures = !s.custom_textures;
        break;
    default:
        break;
    }
}

bool IsPerGameEditable(SettingRowIdx item) {
    switch (item) {
    // Custom Textures / Preload / Dump: only meaningful together with a texture pack on disk,
    // not something you'd flip mid-session.
    case SettingRowCustomTextures:
    case SettingRowPreloadTextures:
    case SettingRowDumpTextures:
    // Region / Language / New 3DS mode: read once at boot; changing them mid-session has no
    // effect until the next launch.
    case SettingRowNew3ds:
    case SettingRowRegion:
    case SettingRowLanguage:
    // CPU JIT is a plain global Setting (no SwitchableSetting global/custom split), so it can't
    // be made a per-game override at all — flipping it in-game would either do nothing or leak
    // into the global default for every other title.
    case SettingRowCpuJit:
    case SettingRowAsyncGpu:
    case SettingRowStrictGpuSync:
    // R3 Screen Layouts opens its own multi-select picker in the library (OpenLayoutPicker),
    // which the in-game screen doesn't have; not a fit for the arm-and-cycle row model.
    case SettingRowLayoutCycle:
        return false;
    default:
        return true;
    }
}

} // namespace SwitchFrontend
