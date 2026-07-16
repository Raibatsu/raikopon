// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <memory>

#include "citra_switch/config.h"
#include "citra_switch/menu_data.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "common/settings.h"
#include "core/loader/loader.h"
#include "core/loader/smdh.h"

namespace SwitchFrontend {

namespace {

// Decode game icons
std::uint32_t Rgb565ToRgba8888(std::uint16_t c) {
    const std::uint32_t r5 = (c >> 11) & 0x1F;
    const std::uint32_t g6 = (c >> 5) & 0x3F;
    const std::uint32_t b5 = c & 0x1F;
    const std::uint32_t r = (r5 << 3) | (r5 >> 2);
    const std::uint32_t g = (g6 << 2) | (g6 >> 4);
    const std::uint32_t b = (b5 << 3) | (b5 >> 2);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

// Trims a UTF-16 title into a single clean UTF-8 line.
std::string CleanTitle(const std::array<char16_t, 0x80>& raw) {
    std::u16string u16{raw.data(),
                       std::char_traits<char16_t>::length(raw.data()) > raw.size()
                           ? raw.size()
                           : std::char_traits<char16_t>::length(raw.data())};
    std::string out = Common::UTF16ToUTF8(u16);
    for (char& ch : out) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    // Collapse the runs of spaces the newline replacement leaves.
    std::string collapsed;
    collapsed.reserve(out.size());
    bool prev_space = false;
    for (char ch : out) {
        const bool space = ch == ' ';
        if (space && prev_space) {
            continue;
        }
        collapsed.push_back(ch);
        prev_space = space;
    }
    while (!collapsed.empty() && collapsed.back() == ' ') {
        collapsed.pop_back();
    }
    return collapsed;
}

void FillFromSmdh(GameEntry& entry, const Loader::SMDH& smdh) {
    using Language = Loader::SMDH::TitleLanguage;
    const auto title = CleanTitle(smdh.GetLongTitle(Language::English));
    if (!title.empty()) {
        entry.title = title;
    }
    const auto& pub = smdh.titles[static_cast<std::size_t>(Language::English)].publisher;
    entry.publisher = Common::UTF16ToUTF8(std::u16string{pub.data(),
        std::char_traits<char16_t>::length(pub.data())});

    const std::vector<u16> icon = smdh.GetIcon(true);
    if (icon.size() == 48 * 48) {
        entry.icon_size = 48;
        entry.icon.resize(icon.size());
        std::transform(icon.begin(), icon.end(), entry.icon.begin(), Rgb565ToRgba8888);
    }
}

// Reads one candidate file into a GameEntry, or returns false if it isn't a title.
bool TryLoad(const std::string& path, GameEntry& entry) {
    std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(path);
    if (!loader) {
        return false;
    }
    bool is_executable = false;
    if (loader->IsExecutable(is_executable) != Loader::ResultStatus::Success || !is_executable) {
        return false;
    }

    entry.path = path;
    entry.title = std::string{FileUtil::GetFilename(path)};
    entry.file_type = Loader::GetFileTypeString(loader->GetFileType(), loader->IsFileCompressed());

    std::vector<u8> smdh_buffer;
    const Loader::ResultStatus icon_result = loader->ReadIcon(smdh_buffer);
    if (icon_result == Loader::ResultStatus::ErrorEncrypted) {
        entry.encrypted = true;
    } else if (icon_result == Loader::ResultStatus::Success &&
               Loader::IsValidSMDH(smdh_buffer)) {
        Loader::SMDH smdh;
        std::memcpy(&smdh, smdh_buffer.data(), sizeof(Loader::SMDH));
        FillFromSmdh(entry, smdh);
    }
    return true;
}

void ScanDirectory(const std::string& directory, std::vector<GameEntry>& out, int depth) {
    if (depth > 4) {
        return;
    }
    FileUtil::ForeachDirectoryEntry(
        nullptr, directory,
        [&out, depth](u64*, const std::string& dir, const std::string& virtual_name) {
            const std::string path = dir + virtual_name;
            if (FileUtil::IsDirectory(path)) {
                ScanDirectory(path + '/', out, depth + 1);
                return true;
            }
            GameEntry entry;
            if (TryLoad(path, entry)) {
                out.push_back(std::move(entry));
            }
            return true;
        });
}

} // namespace

std::vector<GameEntry> ScanGames() {
    const std::string roms_dir = FileUtil::GetUserPath(FileUtil::UserPath::UserDir) + "roms/";
    FileUtil::CreateFullPath(roms_dir);

    std::vector<GameEntry> games;
    ScanDirectory(roms_dir, games, 0);

    std::sort(games.begin(), games.end(), [](const GameEntry& a, const GameEntry& b) {
        return Common::ToLower(a.title) < Common::ToLower(b.title);
    });
    return games;
}

MenuSettings GetMenuSettings() {
    const auto& v = Settings::values;
    return MenuSettings{
        .resolution_factor = static_cast<int>(v.resolution_factor.GetValue()),
        .use_vsync = v.use_vsync.GetValue(),
        .async_shader_compilation = v.async_shader_compilation.GetValue(),
        .use_disk_shader_cache = v.use_disk_shader_cache.GetValue(),
        .show_fps = v.show_fps.GetValue(),
        .cpu_clock_percentage = static_cast<int>(v.cpu_clock_percentage.GetValue()),
        .is_new_3ds = v.is_new_3ds.GetValue(),
        .use_cpu_jit = v.use_cpu_jit.GetValue(),
        .region_value = static_cast<int>(v.region_value.GetValue()),
        .graphics_api = static_cast<int>(Settings::GetWorkingGraphicsAPI()),
    };
}

void SetMenuSettings(const MenuSettings& s) {
    auto& v = Settings::values;
    v.resolution_factor = static_cast<u32>(std::clamp(s.resolution_factor, 0, 10));
    v.use_vsync = s.use_vsync;
    v.async_shader_compilation = s.async_shader_compilation;
    v.use_disk_shader_cache = s.use_disk_shader_cache;
    v.show_fps = s.show_fps;
    v.cpu_clock_percentage = std::clamp(s.cpu_clock_percentage, 5, 400);
    v.is_new_3ds = s.is_new_3ds;
    v.use_cpu_jit = s.use_cpu_jit;
    v.region_value = std::clamp(s.region_value, -1, 6);
    SaveConfig();
}

} // namespace SwitchFrontend
