// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/library_mods.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

#include "citra_switch/ui_paths.h"

namespace SwitchFrontend {

namespace {

struct ModEntry {
    std::string name;
    bool enabled;
};

std::optional<std::uint64_t> s_loaded_title_id;
std::vector<ModEntry> s_mods;
bool s_dirty = false;

} // namespace

void LoadTitleMods(std::uint64_t title_id) {
    if (s_loaded_title_id.has_value() && *s_loaded_title_id == title_id) {
        return;
    }
    s_loaded_title_id = title_id;
    s_dirty = false;

    const std::vector<std::string> disabled = GetDisabledMods(title_id);
    s_mods.clear();
    for (std::string& name : DiscoverTitleMods(title_id)) {
        const bool is_disabled = std::find(disabled.begin(), disabled.end(), name) != disabled.end();
        s_mods.push_back({std::move(name), !is_disabled});
    }
}

int ModCount() {
    return static_cast<int>(s_mods.size());
}

std::string ModName(int index) {
    return index >= 0 && index < static_cast<int>(s_mods.size())
              ? s_mods[static_cast<std::size_t>(index)].name
              : std::string{};
}

bool ModEnabled(int index) {
    return index >= 0 && index < static_cast<int>(s_mods.size()) &&
          s_mods[static_cast<std::size_t>(index)].enabled;
}

void ToggleMod(int index) {
    if (index < 0 || index >= static_cast<int>(s_mods.size())) {
        return;
    }
    ModEntry& mod = s_mods[static_cast<std::size_t>(index)];
    mod.enabled = !mod.enabled;
    s_dirty = true;
}

void PersistTitleMods() {
    if (!s_dirty || !s_loaded_title_id.has_value()) {
        return;
    }
    std::vector<std::string> disabled;
    for (const ModEntry& mod : s_mods) {
        if (!mod.enabled) {
            disabled.push_back(mod.name);
        }
    }
    SetDisabledMods(*s_loaded_title_id, disabled);
    s_dirty = false;
}

} // namespace SwitchFrontend
