// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

// Frontend UI translations, generated into src/citra_switch/lang/<code>.json by
// tools/gen_translations.py - one flat {"string.id": "text"} file per language, not one big file
// with every language nested under each key, so a translator only ever needs to open their own
// language's file. See that script for the full string table and how to add/edit entries.
namespace SwitchFrontend {

// Reads every lang/*.json from romfs into memory once. Cheap (a few KB total), safe to call more
// than once (a no-op after the first attempt). Call once at boot.
void LoadTranslations();

// Sets which language Tr() looks up (a Service::CFG::SystemLanguage value, 0-11 - see
// settings_model.cpp's LanguageName). The frontend's own UI language follows System Language
// directly, same setting games see - there's no separate "UI Language" option.
void SetUiLanguage(int cfg_language);

// Looks up `key` in the current UI language, falling back to English if that language doesn't
// have this key yet (translations are an ongoing, community-editable effort - see
// tools/gen_translations.py - so gaps are expected), and finally to the key itself if even
// English is missing it (a bug - every key must exist in en.json - but visibly showing the raw
// key beats silently showing nothing).
std::string Tr(std::string_view key);

} // namespace SwitchFrontend
