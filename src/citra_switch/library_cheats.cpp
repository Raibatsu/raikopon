// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/library_cheats.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <fmt/format.h>

#include "citra_switch/keyboard_prompt.h"
#include "citra_switch/ui_strings.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/cheats/cheat_base.h"
#include "core/cheats/gateway_cheat.h"

namespace SwitchFrontend {

namespace {

std::optional<std::uint64_t> s_loaded_title_id;
std::vector<std::shared_ptr<Cheats::CheatBase>> s_cheats;
bool s_dirty = false;

std::string CheatFilePath(std::uint64_t title_id) {
    const std::string cheat_dir = FileUtil::GetUserPath(FileUtil::UserPath::CheatsDir);
    return fmt::format("{}{:016X}.txt", cheat_dir, title_id);
}

} // namespace

void LoadLibraryCheats(std::uint64_t title_id) {
    if (s_loaded_title_id.has_value() && *s_loaded_title_id == title_id) {
        return;
    }
    s_loaded_title_id = title_id;
    s_cheats = Cheats::GatewayCheat::LoadFile(CheatFilePath(title_id));
    s_dirty = false;
}

int LibraryCheatCount() {
    return static_cast<int>(s_cheats.size());
}

std::string LibraryCheatName(int index) {
    return index >= 0 && index < static_cast<int>(s_cheats.size()) ? s_cheats[static_cast<std::size_t>(index)]->GetName()
                                                                    : "";
}

bool LibraryCheatEnabled(int index) {
    return index >= 0 && index < static_cast<int>(s_cheats.size()) &&
          s_cheats[static_cast<std::size_t>(index)]->IsEnabled();
}

void ToggleLibraryCheat(int index) {
    if (index < 0 || index >= static_cast<int>(s_cheats.size())) {
        return;
    }
    Cheats::CheatBase& cheat = *s_cheats[static_cast<std::size_t>(index)];
    cheat.SetEnabled(!cheat.IsEnabled());
    s_dirty = true;
}

void PersistLibraryCheats() {
    if (!s_dirty || !s_loaded_title_id.has_value()) {
        return;
    }
    const std::string cheat_dir = FileUtil::GetUserPath(FileUtil::UserPath::CheatsDir);
    if (!FileUtil::IsDirectory(cheat_dir)) {
        FileUtil::CreateDir(cheat_dir);
    }
    const std::string filepath = CheatFilePath(*s_loaded_title_id);
    FileUtil::IOFile file(filepath, "w");
    for (const auto& cheat : s_cheats) {
        file.WriteString(cheat->ToString());
    }
    s_dirty = false;
}

int EditLibraryCheatFlow(int edit_index) {
    std::string initial_name;
    std::vector<std::string> initial_lines;
    if (edit_index >= 0) {
        if (edit_index >= static_cast<int>(s_cheats.size())) {
            return -1;
        }
        const Cheats::CheatBase& cheat = *s_cheats[static_cast<std::size_t>(edit_index)];
        initial_name = cheat.GetName();
        initial_lines = Common::SplitString(cheat.GetCode(), '\n');
        while (!initial_lines.empty() && initial_lines.back().empty()) {
            initial_lines.pop_back();
        }
    }

    const std::string name =
        PromptKeyboard(Tr("cheats.name_title"), Tr("cheats.name_guide"), initial_name, 64);
    if (name.empty()) {
        return -1;
    }

    std::vector<std::string> lines;
    for (std::size_t i = 0;; ++i) {
        const std::string existing = i < initial_lines.size() ? initial_lines[i] : std::string{};
        const std::string header = Tr("cheats.code_line_prefix") + std::to_string(i + 1);
        const std::string guide =
            existing.empty() ? Tr("cheats.guide_finish") : Tr("cheats.guide_clear");
        const std::string line = Common::StripSpaces(PromptKeyboard(header, guide, existing, 32));
        if (line.empty()) {
            break;
        }
        lines.push_back(line);
    }
    if (lines.empty()) {
        return -1;
    }

    std::string code;
    for (const std::string& line : lines) {
        code += line + '\n';
    }

    auto cheat = std::make_shared<Cheats::GatewayCheat>(name, code, std::string{});
    int result_index = edit_index;
    if (edit_index >= 0) {
        s_cheats[static_cast<std::size_t>(edit_index)] = std::move(cheat);
    } else {
        result_index = LibraryCheatCount();
        s_cheats.push_back(std::move(cheat));
    }
    s_dirty = true;
    return result_index;
}

int DeleteLibraryCheatFlow(int index) {
    if (index < 0 || index >= LibraryCheatCount()) {
        return index;
    }
    s_cheats.erase(s_cheats.begin() + index);
    s_dirty = true;
    return index;
}

} // namespace SwitchFrontend
