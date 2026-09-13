// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_strings.h"

#include <array>
#include <unordered_map>

#include <json.hpp>

#include "common/file_util.h"
#include "common/logging/log.h"

namespace SwitchFrontend {

namespace {

// Order matches Service::CFG::SystemLanguage / libnx's SetLanguage (0-11) - see
// settings_model.cpp's LanguageName and glyph_atlas.cpp's SetLanguage for the same ordering
// assumption elsewhere in the frontend.
constexpr std::array<const char*, 12> kLanguageCodes = {
    "ja", "en", "fr", "de", "it", "es", "zhcn", "ko", "nl", "pt", "ru", "zhtw",
};
constexpr int kEnglishIndex = 1;

std::array<std::unordered_map<std::string, std::string>, kLanguageCodes.size()> s_strings;
bool s_loaded = false;
int s_current_language = kEnglishIndex;

} // namespace

void LoadTranslations() {
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    for (std::size_t i = 0; i < kLanguageCodes.size(); ++i) {
        const std::string path = std::string("romfs:/lang/") + kLanguageCodes[i] + ".json";
        std::string buffer;
        if (!FileUtil::Exists(path) || !FileUtil::ReadFileToString(true, path, buffer)) {
            LOG_WARNING(Frontend, "Translation file '{}' not found", path);
            continue;
        }
        try {
            const nlohmann::json parsed = nlohmann::json::parse(buffer);
            for (auto it = parsed.begin(); it != parsed.end(); ++it) {
                if (it.value().is_string()) {
                    s_strings[i][it.key()] = it.value().get<std::string>();
                }
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR(Frontend, "Failed to parse '{}': {}", path, e.what());
        }
    }
    LOG_INFO(Frontend, "Loaded translations: {} English strings", s_strings[kEnglishIndex].size());
}

void SetUiLanguage(int cfg_language) {
    if (cfg_language < 0 || cfg_language >= static_cast<int>(kLanguageCodes.size())) {
        cfg_language = kEnglishIndex;
    }
    s_current_language = cfg_language;
}

std::string Tr(std::string_view key) {
    const std::string key_str{key};

    const auto& current = s_strings[static_cast<std::size_t>(s_current_language)];
    const auto current_it = current.find(key_str);
    if (current_it != current.end()) {
        return current_it->second;
    }

    const auto& english = s_strings[kEnglishIndex];
    const auto english_it = english.find(key_str);
    if (english_it != english.end()) {
        return english_it->second;
    }

    return key_str;
}

} // namespace SwitchFrontend
