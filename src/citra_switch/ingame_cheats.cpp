// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ingame_cheats.h"

#include <memory>
#include <vector>

#include "citra_switch/keyboard_prompt.h"
#include "common/string_util.h"
#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
#include "core/cheats/gateway_cheat.h"
#include "core/core.h"
#include "core/loader/loader.h"

namespace SwitchFrontend {
namespace {

bool s_dirty = false;

Cheats::CheatEngine* GetCheatEngine() {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return nullptr;
    }
    return &system.CheatEngine();
}

} // namespace

int CheatCount() {
    auto* engine = GetCheatEngine();
    return engine ? static_cast<int>(engine->GetCheats().size()) : 0;
}

std::string CheatName(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return "";
    }
    const auto cheats = engine->GetCheats();
    return index >= 0 && index < static_cast<int>(cheats.size()) ? cheats[index]->GetName() : "";
}

bool CheatEnabled(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return false;
    }
    const auto cheats = engine->GetCheats();
    return index >= 0 && index < static_cast<int>(cheats.size()) && cheats[index]->IsEnabled();
}

void ToggleCheat(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return;
    }
    const auto cheats = engine->GetCheats();
    if (index < 0 || index >= static_cast<int>(cheats.size())) {
        return;
    }
    cheats[index]->SetEnabled(!cheats[index]->IsEnabled());
    s_dirty = true;
}

void PersistCheats() {
    if (!s_dirty) {
        return;
    }
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        u64 title_id = 0;
        system.GetAppLoader().ReadProgramId(title_id);
        system.CheatEngine().SaveCheatFile(title_id);
    }
    s_dirty = false;
}

int EditCheatFlow(int edit_index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return -1;
    }

    std::string initial_name;
    std::vector<std::string> initial_lines;
    if (edit_index >= 0) {
        const auto cheats = engine->GetCheats();
        if (edit_index >= static_cast<int>(cheats.size())) {
            return -1;
        }
        initial_name = cheats[edit_index]->GetName();
        initial_lines = Common::SplitString(cheats[edit_index]->GetCode(), '\n');
        // GetCode() puts a trailing '\n' after the last line too, which SplitString turns into
        // one trailing empty entry.
        while (!initial_lines.empty() && initial_lines.back().empty()) {
            initial_lines.pop_back();
        }
    }

    const std::string name = PromptKeyboard("Cheat name", "e.g. Infinite HP", initial_name, 64);
    if (name.empty()) {
        return -1;
    }

    std::vector<std::string> lines;
    for (std::size_t i = 0;; ++i) {
        const std::string existing = i < initial_lines.size() ? initial_lines[i] : std::string{};
        const std::string header = "Cheat code - line " + std::to_string(i + 1);
        const std::string guide = existing.empty() ? "XXXXXXXX YYYYYYYY - blank line to finish"
                                                    : "XXXXXXXX YYYYYYYY - blank clears this line";
        // Stripped defensively: GatewayCheat's line parser requires exactly 17 characters
        // ("XXXXXXXX YYYYYYYY") with no slack, and a touch keyboard is an easy way to pick up a
        // stray leading/trailing space that would otherwise silently invalidate the line.
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
        engine->UpdateCheat(static_cast<std::size_t>(edit_index), std::move(cheat));
    } else {
        result_index = CheatCount();
        engine->AddCheat(std::move(cheat));
    }
    s_dirty = true;
    return result_index;
}

int DeleteCheatFlow(int index) {
    auto* engine = GetCheatEngine();
    if (!engine) {
        return index;
    }
    if (index < 0 || index >= CheatCount()) {
        return index;
    }
    engine->RemoveCheat(static_cast<std::size_t>(index));
    s_dirty = true;
    return index;
}

} // namespace SwitchFrontend
