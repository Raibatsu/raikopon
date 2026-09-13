// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "citra_switch/config.h"

namespace SwitchFrontend {

enum class TitleKind {
    Application,
    Demo,
    Update,
    AddOnContent,
    System,
    Other,
};

struct GameEntry {
    std::string path;
    std::string title;
    std::string publisher;
    std::string file_type;
    bool encrypted{};
    bool installed{};
    bool insertable{};
    std::uint64_t program_id{};
    // e.g. "CTR-P-BNDE" - empty for homebrew/ELF/3DSX, which have no product code. See
    // GameTdbGameId() below for the 4-character ID GameTDB actually keys box art and title
    // descriptions by.
    std::string product_code;
    int icon_size{};
    std::vector<std::uint32_t> icon;
    std::uint64_t total_playtime_seconds{};
    std::uint64_t last_played{};
};

struct TitleDetails {
    std::uint64_t program_id{};
    TitleKind kind{};
    bool has_base_version{};
    std::uint16_t base_version{};
    bool has_update{};
    std::uint16_t update_version{};
    bool has_dlc{};
    int dlc_contents{};
};

struct CiaEntry {
    std::string name;
    std::string path;
    std::uint64_t program_id{};
    TitleKind kind{};
    std::uint16_t version{};
    std::uint64_t size{};
    bool compressed{};
    bool readable{};
};

enum class InstallResult {
    Success,
    FileNotFound,
    FailedToOpen,
    Aborted,
    Invalid,
    Encrypted,
};

struct DirEntry {
    std::string name;
    std::string path;
};

struct MenuSettings {
    int resolution_factor{};
    int layout_preset{};
    bool use_vsync{};
    bool async_gpu_emulation{};
    bool strict_gpu_sync{};
    bool async_shader_compilation{};
    bool use_disk_shader_cache{};
    bool use_hw_shader{};
    bool use_ubershaders{};
    bool disable_pipeline_fast_path{};
    bool skip_slow_draw{};
    bool skip_texture_copy{};
    bool skip_cpu_write{};
    bool enable_compile_boost{};
    bool enable_gpu_frame_log{};
    bool disable_right_eye_render{};
    int texture_filter{};
    bool use_integer_scaling{};
    bool filter_mode{};
    bool show_fps{};
    bool show_shader_compile_progress{};
    bool custom_textures{};
    bool preload_textures{};
    bool dump_textures{};
    int cpu_clock_percentage{};
    int movie_throttle_clock_percentage{};
    bool movie_throttle_enabled{};
    bool gametdb_enabled{};
    bool is_new_3ds{};
    bool plugin_loader_enabled{};
    bool allow_plugin_loader{};
    bool use_cpu_jit{};
    bool fastmem{};
    int region_value{};
    int language{};
    int graphics_api{};
    int pointer_source{};
    int gyro_sensitivity_x{};
    int gyro_sensitivity_y{};
    std::uint32_t layout_cycle_mask{};
};

std::vector<GameEntry> ScanGames();

TitleDetails GetTitleDetails(const GameEntry& entry);

// The last 4 characters of an NCCH product code (e.g. "CTR-P-BNDE" -> "BNDE") - the ID GameTDB
// itself keys both box art (see gametdb.h) and title descriptions (see titledb.h) by. Empty if
// `product_code` doesn't look like a real product code (homebrew has none).
std::string GameTdbGameId(const std::string& product_code);

TitleKind ClassifyTitle(std::uint64_t program_id);

std::string TitleKindName(TitleKind kind);

std::string FormatTitleVersion(std::uint16_t version);

bool GetInstalledVersion(std::uint64_t program_id, std::uint16_t& version);

std::vector<CiaEntry> ListCiaFiles(const std::string& directory);

InstallResult InstallCia(const std::string& path,
                         const std::function<void(std::size_t, std::size_t)>& progress);

std::string InstallResultText(InstallResult result);

int ClearShaderCache(std::uint64_t program_id);

std::vector<DirEntry> ListSubdirectories(const std::string& directory);

std::string ParentDirectory(const std::string& directory);

bool EnsureDirectory(const std::string& directory);

MenuSettings GetMenuSettings();

MenuSettings DefaultMenuSettings();

void SetMenuSettings(const MenuSettings& settings);

// Same as SetMenuSettings, but skips the SaveConfig() disk write - for callers that apply many
// times in quick succession (a cyclable/gyro row auto-repeating while held) and will call
// SaveConfig() themselves once the user settles, rather than writing the whole config file to
// disk on every single step.
void ApplyMenuSettings(const MenuSettings& settings);

} // namespace SwitchFrontend
