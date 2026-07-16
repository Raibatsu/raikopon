// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Data the menu draws produced by the Azahar core.
namespace SwitchFrontend {

// One scanned title in sdmc:/switch/dekopon/roms/.
struct GameEntry {
    std::string path;      // Absolute SD path passed to BootRom.
    std::string title;     // SMDH long title, or the file name as a fallback.
    std::string publisher; // SMDH publisher, empty if unknown.
    std::string file_type;
    bool encrypted{};      // True if the SMDH couldn't be read due to missing keys.
    int icon_size{};       // 48x48 RGBA8888 icon, empty if none.
    std::vector<std::uint32_t> icon;
};

// The subset of settings the on-console menu can edit.
struct MenuSettings {
    int resolution_factor{};      // 0 = auto (window), 1 = native, 2... = upscale factor.
    bool use_vsync{};
    bool async_shader_compilation{};
    bool use_disk_shader_cache{};
    bool show_fps{};              // On-screen framerate counter.
    int cpu_clock_percentage{};   // 5..400.
    bool is_new_3ds{};
    bool use_cpu_jit{};
    int region_value{};           // -1 = auto, 0..6 per SMDH region order.
    int graphics_api{};           // Active graphics API backend.
};

// Scans the ROM directory and returns the loadable titles, sorted by title.
std::vector<GameEntry> ScanGames();

// Snapshots the editable settings from Settings::values.
MenuSettings GetMenuSettings();

// Applies edited settings to Settings::values and saves config.ini.
void SetMenuSettings(const MenuSettings& settings);

} // namespace SwitchFrontend
