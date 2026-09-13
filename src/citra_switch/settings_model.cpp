// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/settings_model.h"
#include "citra_switch/ui_strings.h"

namespace SwitchFrontend {

const std::array<std::pair<SettingsTab, const char*>, 7> kSettingsTabs{{
    {SettingsTab::Display, "Display"},
    {SettingsTab::Performance, "Performance"},
    {SettingsTab::Advanced, "Advanced"},
    {SettingsTab::System, "System"},
    {SettingsTab::Paths, "Paths"},
    {SettingsTab::Controls, "Controls"},
    {SettingsTab::Updates, "Updates"},
}};

namespace {
SettingRow Header(const std::string& label) {
    return {SettingRowSectionHeader, label, "", "", true};
}
} // namespace

std::string RegionName(int region) {
    switch (region) {
    case -1:
        return Tr("value.region.auto");
    case 0:
        return Tr("value.region.japan");
    case 1:
        return Tr("value.region.usa");
    case 2:
        return Tr("value.region.europe");
    case 3:
        return Tr("value.region.australia");
    case 4:
        return Tr("value.region.china");
    case 5:
        return Tr("value.region.korea");
    case 6:
        return Tr("value.region.taiwan");
    default:
        return Tr("value.region.auto");
    }
}

// Ordered to match Service::CFG::SystemLanguage. Each name is spelled in its own language
// (native self-name), not translated into the current UI language - matches how a language
// picker is conventionally shown.
const char* LanguageName(int language) {
    switch (language) {
    case 0:
        return "日本語";
    case 1:
        return "English";
    case 2:
        return "Français";
    case 3:
        return "Deutsch";
    case 4:
        return "Italiano";
    case 5:
        return "Español";
    case 6:
        return "简体中文";
    case 7:
        return "한국어";
    case 8:
        return "Nederlands";
    case 9:
        return "Português";
    case 10:
        return "Русский";
    case 11:
        return "繁體中文";
    default:
        return "English";
    }
}

std::string TextureFilterName(int filter) {
    switch (filter) {
    case 0:
        return Tr("common.none");
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
        return Tr("common.none");
    }
}

std::string ResolutionText(int factor) {
    if (factor == 0) {
        return Tr("value.resolution.auto");
    }
    if (factor == 1) {
        return Tr("value.resolution.native");
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
    return std::to_string(enabled) + " " + Tr("value.layout_cycle.of") + " " + std::to_string(total);
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
    case SettingsTab::Display:
        return {
            Header(Tr("header.resolution_filtering")),
            {SettingRowResolution, Tr("settings.row.resolution"), ResolutionText(s.resolution_factor),
             Tr("settings.row.resolution.desc")},
            {SettingRowTextureFilter, Tr("settings.row.texture_filter"), TextureFilterName(s.texture_filter),
             Tr("settings.row.texture_filter.desc")},
            {SettingRowLinearFiltering, Tr("settings.row.linear_filtering"), s.filter_mode ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.linear_filtering.desc")},
            {SettingRowIntegerScaling, Tr("settings.row.integer_scaling"), s.use_integer_scaling ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.integer_scaling.desc")},
            Header(Tr("header.screen")),
            {SettingRowVSync, Tr("settings.row.vsync"), s.use_vsync ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.vsync.desc")},
            {SettingRowShowFps, Tr("settings.row.show_fps"), s.show_fps ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.show_fps.desc")},
            {SettingRowShowShaderCompileProgress, Tr("settings.row.show_shader_compile_progress"),
             s.show_shader_compile_progress ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.show_shader_compile_progress.desc")},
            {SettingRowLayout, Tr("settings.row.layout"), GetScreenLayoutName(s.layout_preset),
             Tr("settings.row.layout.desc")},
            {SettingRowLayoutCycle, Tr("settings.row.layout_cycle"), LayoutCycleSummary(s.layout_cycle_mask),
             Tr("settings.row.layout_cycle.desc")},
        };
    case SettingsTab::Performance: {
        std::vector<SettingRow> rows = {
            Header(Tr("header.gpu_shaders")),
            {SettingRowAsyncGpu, Tr("settings.row.async_gpu"), s.async_gpu_emulation ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.async_gpu.desc")},
            {SettingRowStrictGpuSync, Tr("settings.row.strict_gpu_sync"), s.strict_gpu_sync ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.strict_gpu_sync.desc")},
            {SettingRowAsyncShaders, Tr("settings.row.async_shaders"),
             s.async_shader_compilation ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.async_shaders.desc")},
            {SettingRowDiskShaderCache, Tr("settings.row.disk_shader_cache"), s.use_disk_shader_cache ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.disk_shader_cache.desc")},
            {SettingRowHwShader, Tr("settings.row.hw_shader"), s.use_hw_shader ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.hw_shader.desc")},
            {SettingRowUbershaders, Tr("settings.row.ubershaders"), s.use_ubershaders ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.ubershaders.desc")},
            {SettingRowEnableCompileBoost, Tr("settings.row.compile_boost"),
             s.enable_compile_boost ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.compile_boost.desc")},
            {SettingRowDisableRightEye, Tr("settings.row.disable_right_eye"),
             s.disable_right_eye_render ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.disable_right_eye.desc")},
            Header(Tr("header.cpu")),
            {SettingRowCpuClock, Tr("settings.row.cpu_clock"), std::to_string(s.cpu_clock_percentage) + "%",
             Tr("settings.row.cpu_clock.desc")},
            Header(Tr("header.movie_throttle")),
            {SettingRowMovieThrottleEnabled, Tr("settings.row.movie_throttle_enabled"),
             s.movie_throttle_enabled ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.movie_throttle_enabled.desc")},
        };
        if (s.movie_throttle_enabled) {
            rows.push_back({SettingRowMovieThrottle, Tr("settings.row.movie_throttle"),
                            std::to_string(s.movie_throttle_clock_percentage) + "%",
                            Tr("settings.row.movie_throttle.desc")});
        }
        return rows;
    }
    case SettingsTab::Advanced:
        return {
            Header(Tr("header.core")),
            {SettingRowCpuJit, Tr("settings.row.cpu_jit"), s.use_cpu_jit ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.cpu_jit.desc")},
            {SettingRowFastmem, Tr("settings.row.fastmem"), s.fastmem ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.fastmem.desc")},
            Header(Tr("header.rendering_debug")),
            // Disable Pipeline Fast Path row intentionally hidden - user testing found toggling it
            // made no measurable difference (see docs/FORK_OVERVIEW.md), and the config loader
            // (config.cpp's ReadValues) now pins the underlying setting off rather than reading it
            // from config.ini, so there's nothing left to expose here.
            {SettingRowSkipSlowDraw, Tr("settings.row.skip_slow_draw"), s.skip_slow_draw ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.skip_slow_draw.desc")},
            {SettingRowSkipTextureCopy, Tr("settings.row.skip_texture_copy"), s.skip_texture_copy ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.skip_texture_copy.desc")},
            {SettingRowSkipCpuWrite, Tr("settings.row.skip_cpu_write"), s.skip_cpu_write ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.skip_cpu_write.desc")},
        };
    case SettingsTab::System:
        return {
            Header(Tr("header.console")),
            {SettingRowNew3ds, Tr("settings.row.new3ds"), s.is_new_3ds ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.new3ds.desc")},
            {SettingRowRegion, Tr("settings.row.region"), RegionName(s.region_value),
             Tr("settings.row.region.desc")},
            {SettingRowLanguage, Tr("settings.row.language"), LanguageName(s.language),
             Tr("settings.row.language.desc")},
            {SettingRowPluginLoader, Tr("settings.row.plugin_loader"),
             s.plugin_loader_enabled ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.plugin_loader.desc")},
            {SettingRowAllowPluginLoader, Tr("settings.row.allow_plugin_loader"),
             s.allow_plugin_loader ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.allow_plugin_loader.desc")},
            Header(Tr("header.custom_textures")),
            {SettingRowCustomTextures, Tr("settings.row.custom_textures"), s.custom_textures ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.custom_textures.desc")},
            {SettingRowPreloadTextures, Tr("settings.row.preload_textures"),
             s.preload_textures ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.preload_textures.desc")},
            {SettingRowDumpTextures, Tr("settings.row.dump_textures"), s.dump_textures ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.dump_textures.desc")},
            Header(Tr("header.box_art")),
            {SettingRowGameTdbEnabled, Tr("settings.row.gametdb_enabled"),
             s.gametdb_enabled ? Tr("common.on") : Tr("common.off"),
             Tr("settings.row.gametdb_enabled.desc")},
            {SettingRowDownloadCovers, Tr("settings.row.download_covers"),
             Tr("settings.row.download_covers.value"),
             Tr("settings.row.download_covers.desc")},
        };
    case SettingsTab::Paths:
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
    case SettingRowLayout:
        s.layout_preset = std::clamp(s.layout_preset + dir, 0, GetScreenLayoutCount() - 1);
        break;
    case SettingRowTextureFilter:
        s.texture_filter = std::clamp(s.texture_filter + dir, 0, 5);
        break;
    case SettingRowCpuClock:
        // 1% steps like the movie throttle row, so hold-to-repeat scrubs it like a slider.
        s.cpu_clock_percentage = std::clamp(s.cpu_clock_percentage + dir, 25, 400);
        break;
    case SettingRowMovieThrottle:
        s.movie_throttle_clock_percentage =
            std::clamp(s.movie_throttle_clock_percentage + dir, 10, 100);
        break;
    case SettingRowRegion:
        s.region_value = std::clamp(s.region_value + dir, -1, 6);
        break;
    case SettingRowLanguage: {
        // Korean (index 7) is skipped - its shared font doesn't render correctly here yet, unlike
        // every other language, and this only steers the *frontend UI's* language selection away
        // from it (the row's underlying value is still a real Service::CFG::SystemLanguage code,
        // shared with the emulated 3DS's own region setting, so the numbering itself can't change).
        int lang = std::clamp(s.language + dir, 0, 11);
        if (lang == 7) {
            lang = std::clamp(lang + dir, 0, 11);
        }
        s.language = lang;
        break;
    }
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
    case SettingRowUbershaders:
    case SettingRowLinearFiltering:
    case SettingRowIntegerScaling:
    case SettingRowShowFps:
    case SettingRowShowShaderCompileProgress:
    case SettingRowDisableRightEye:
    case SettingRowNew3ds:
    case SettingRowPluginLoader:
    case SettingRowAllowPluginLoader:
    case SettingRowCpuJit:
    case SettingRowFastmem:
    case SettingRowPreloadTextures:
    case SettingRowDumpTextures:
    case SettingRowDisablePipelineFastPath:
    case SettingRowSkipSlowDraw:
    case SettingRowSkipTextureCopy:
    case SettingRowSkipCpuWrite:
    case SettingRowEnableCompileBoost:
    case SettingRowEnableGpuFrameLog:
    case SettingRowCustomTextures:
    case SettingRowMovieThrottleEnabled:
    case SettingRowGameTdbEnabled:
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
    case SettingRowUbershaders:
        s.use_ubershaders = !s.use_ubershaders;
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
    case SettingRowShowShaderCompileProgress:
        s.show_shader_compile_progress = !s.show_shader_compile_progress;
        break;
    case SettingRowDisableRightEye:
        s.disable_right_eye_render = !s.disable_right_eye_render;
        break;
    case SettingRowNew3ds:
        s.is_new_3ds = !s.is_new_3ds;
        break;
    case SettingRowPluginLoader:
        s.plugin_loader_enabled = !s.plugin_loader_enabled;
        break;
    case SettingRowAllowPluginLoader:
        s.allow_plugin_loader = !s.allow_plugin_loader;
        break;
    case SettingRowCpuJit:
        s.use_cpu_jit = !s.use_cpu_jit;
        break;
    case SettingRowFastmem:
        s.fastmem = !s.fastmem;
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
    case SettingRowEnableGpuFrameLog:
        s.enable_gpu_frame_log = !s.enable_gpu_frame_log;
        break;
    case SettingRowCustomTextures:
        s.custom_textures = !s.custom_textures;
        break;
    case SettingRowMovieThrottleEnabled:
        s.movie_throttle_enabled = !s.movie_throttle_enabled;
        break;
    case SettingRowGameTdbEnabled:
        s.gametdb_enabled = !s.gametdb_enabled;
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
    // A global "do I want this feature at all" toggle, not something tied to one title.
    case SettingRowGameTdbEnabled:
    // A global action (kicks off one background download covering every owned game), not a
    // per-title value - same reasoning as GameTdbEnabled just above.
    case SettingRowDownloadCovers:
    // Region / Language / New 3DS mode: read once at boot; changing them mid-session has no
    // effect until the next launch.
    case SettingRowNew3ds:
    case SettingRowRegion:
    case SettingRowLanguage:
    // CPU JIT is a plain global Setting (no SwitchableSetting global/custom split), so it can't
    // be made a per-game override at all — flipping it in-game would either do nothing or leak
    // into the global default for every other title.
    case SettingRowCpuJit:
    case SettingRowFastmem:
    case SettingRowPluginLoader:
    case SettingRowAllowPluginLoader:
    case SettingRowShowShaderCompileProgress:
    case SettingRowAsyncGpu:
    case SettingRowStrictGpuSync:
    // Screen Layouts opens its own multi-select picker (DrawLayoutCyclePicker), which the
    // in-game screen doesn't have; not a fit for the arm-and-cycle row model.
    case SettingRowLayoutCycle:
        return false;
    default:
        return true;
    }
}

namespace {
bool KeepForPerGame(const SettingRow& row) {
    return IsPerGameEditable(row.item) && row.item != SettingRowGyroSensitivity;
}

// Touch Pointer Source/Gyro Sensitivity now live under the library's Controls tab (see
// ui_controls.cpp) rather than as a BuildSettingRows(tab) group, since Controls has its own
// button-remap-driven rendering that doesn't consult BuildSettingRows at all. They still need to
// reach the per-game settings screen and in-game quick menu though (both driven purely by
// BuildPerGameSettingRows), so this feeds them in independently of any single tab.
std::vector<SettingRow> TouchMotionRows(const MenuSettings& s) {
    return {
        Header(Tr("header.touch_motion")),
        {SettingRowPointerSource, Tr("settings.row.pointer_source"),
         PointerSourceName(static_cast<PointerSource>(s.pointer_source)),
         Tr("settings.row.pointer_source.desc")},
        {SettingRowGyroSensitivity, Tr("settings.row.gyro_sensitivity"), GyroSensitivityText(s),
         Tr("settings.row.gyro_sensitivity.desc")},
    };
}
} // namespace

namespace {
// Appends `tab_rows` onto `rows`, dropping any row (and any header whose group ends up empty)
// that KeepForPerGame rejects.
void AppendFiltered(std::vector<SettingRow>& rows, const std::vector<SettingRow>& tab_rows) {
    for (std::size_t i = 0; i < tab_rows.size(); ++i) {
        if (!tab_rows[i].is_header) {
            if (KeepForPerGame(tab_rows[i])) {
                rows.push_back(tab_rows[i]);
            }
            continue;
        }
        // Only keep a header if at least one row before the next header survives the filter -
        // otherwise it's a dangling divider with nothing under it (e.g. "Core"'s CPU JIT and
        // Fastmem are both boot-time-only, so the whole group vanishes per-game).
        bool has_content = false;
        for (std::size_t j = i + 1; j < tab_rows.size() && !tab_rows[j].is_header; ++j) {
            if (KeepForPerGame(tab_rows[j])) {
                has_content = true;
                break;
            }
        }
        if (has_content) {
            rows.push_back(tab_rows[i]);
        }
    }
}
} // namespace

std::vector<SettingRow> BuildPerGameSettingRows(const MenuSettings& s) {
    std::vector<SettingRow> rows;
    for (SettingsTab tab :
        {SettingsTab::Display, SettingsTab::Performance, SettingsTab::Advanced, SettingsTab::System}) {
        AppendFiltered(rows, BuildSettingRows(tab, s));
    }
    AppendFiltered(rows, TouchMotionRows(s));
    return rows;
}

int FirstSelectableRow(const std::vector<SettingRow>& rows) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].is_header) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

int NextSelectableRow(const std::vector<SettingRow>& rows, int index, int dir) {
    if (rows.empty()) {
        return index;
    }
    int idx = index;
    for (;;) {
        const int next = idx + dir;
        if (next < 0 || next >= static_cast<int>(rows.size())) {
            return idx;
        }
        idx = next;
        if (!rows[static_cast<std::size_t>(idx)].is_header) {
            return idx;
        }
    }
}

bool RequiresRestart(SettingRowIdx item) {
    switch (item) {
    // All read once at boot (System::Init / GPU thread / dynarmic JIT setup) — changing them
    // mid-session has no effect until Raika Azahar restarts. Custom Textures/Preload/Dump and R3
    // Layouts are excluded from IsPerGameEditable for unrelated reasons and don't belong here.
    case SettingRowNew3ds:
    case SettingRowRegion:
    case SettingRowLanguage:
    case SettingRowCpuJit:
    case SettingRowFastmem:
    case SettingRowAsyncGpu:
    case SettingRowStrictGpuSync:
    case SettingRowEnableGpuFrameLog:
        return true;
    default:
        return false;
    }
}

} // namespace SwitchFrontend
