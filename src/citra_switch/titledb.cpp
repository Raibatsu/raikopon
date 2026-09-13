// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/titledb.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "common/file_util.h"
#include "common/logging/log.h"

namespace SwitchFrontend {

namespace {

// Matches tools/gen_titledb.py's record layout exactly: 4-byte ASCII id, 6 bytes of small numeric
// fields, then 10 (offset, length) pairs into the text blob that follows the whole index, in the
// exact order STRING_FIELDS lists them in the script. Packed on purpose - this is the file layout
// the script writes, byte for byte, not a struct the compiler is free to rearrange/pad.
#pragma pack(push, 1)
struct StringRef {
    std::uint32_t offset;
    std::uint32_t length;
};

constexpr int kNumSynopsisLanguages = 12;
constexpr int kEnglishLanguageIndex = 1;

struct GameRecord {
    char id[4];
    std::uint16_t release_year;
    std::uint8_t release_month;
    std::uint8_t release_day;
    std::uint8_t wifi_players;
    std::uint8_t input_players;
    StringRef developer;
    StringRef publisher;
    StringRef genre;
    StringRef region;
    StringRef languages;
    StringRef rating_type;
    StringRef rating_value;
    StringRef rating_descriptors;
    StringRef wifi_features;
    StringRef synopsis[kNumSynopsisLanguages];
};
#pragma pack(pop)
static_assert(sizeof(GameRecord) == 178);

std::vector<std::uint8_t> s_data;
const GameRecord* s_records = nullptr;
std::uint32_t s_count = 0;
const char* s_text_base = nullptr;
bool s_load_attempted = false;

std::string_view TextOf(const StringRef& ref) {
    return std::string_view{s_text_base + ref.offset, ref.length};
}

std::string_view SelectSynopsis(const GameRecord& rec, int cfg_language) {
    if (cfg_language >= 0 && cfg_language < kNumSynopsisLanguages) {
        const std::string_view exact = TextOf(rec.synopsis[cfg_language]);
        if (!exact.empty()) {
            return exact;
        }
    }
    const std::string_view english = TextOf(rec.synopsis[kEnglishLanguageIndex]);
    if (!english.empty()) {
        return english;
    }
    for (int i = 0; i < kNumSynopsisLanguages; ++i) {
        const std::string_view candidate = TextOf(rec.synopsis[i]);
        if (!candidate.empty()) {
            return candidate;
        }
    }
    return {};
}

} // namespace

void LoadTitleDatabase() {
    if (s_load_attempted) {
        return;
    }
    s_load_attempted = true;

    FileUtil::IOFile file{"romfs:/titledb.bin", "rb"};
    if (!file.IsOpen()) {
        LOG_WARNING(Frontend, "titledb.bin not found in romfs - title info unavailable");
        return;
    }
    const std::uint64_t size = file.GetSize();
    if (size < 8) {
        return;
    }
    s_data.resize(size);
    if (file.ReadBytes(s_data.data(), size) != size) {
        LOG_WARNING(Frontend, "Failed to read titledb.bin");
        s_data.clear();
        return;
    }
    if (std::memcmp(s_data.data(), "TDB3", 4) != 0) {
        LOG_WARNING(Frontend, "titledb.bin has an unexpected header, ignoring");
        s_data.clear();
        return;
    }
    std::uint32_t count = 0;
    std::memcpy(&count, s_data.data() + 4, sizeof(count));
    const std::uint64_t index_bytes = static_cast<std::uint64_t>(count) * sizeof(GameRecord);
    if (8 + index_bytes > size) {
        LOG_WARNING(Frontend, "titledb.bin is truncated, ignoring");
        s_data.clear();
        return;
    }

    s_count = count;
    s_records = reinterpret_cast<const GameRecord*>(s_data.data() + 8);
    s_text_base = reinterpret_cast<const char*>(s_data.data() + 8 + index_bytes);
    LOG_INFO(Frontend, "Loaded titledb.bin: {} titles", s_count);
}

std::optional<TitleInfo> GetTitleInfo(std::string_view game_id, int cfg_language) {
    if (s_records == nullptr || game_id.size() != 4) {
        return std::nullopt;
    }
    std::uint32_t lo = 0;
    std::uint32_t hi = s_count;
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        const int cmp = std::memcmp(s_records[mid].id, game_id.data(), 4);
        if (cmp < 0) {
            lo = mid + 1;
        } else if (cmp > 0) {
            hi = mid;
        } else {
            const GameRecord& rec = s_records[mid];
            TitleInfo info;
            info.developer = TextOf(rec.developer);
            info.publisher = TextOf(rec.publisher);
            info.genre = TextOf(rec.genre);
            info.region = TextOf(rec.region);
            info.languages = TextOf(rec.languages);
            info.rating_type = TextOf(rec.rating_type);
            info.rating_value = TextOf(rec.rating_value);
            info.rating_descriptors = TextOf(rec.rating_descriptors);
            info.wifi_features = TextOf(rec.wifi_features);
            info.synopsis = SelectSynopsis(rec, cfg_language);
            info.release_year = rec.release_year;
            info.release_month = rec.release_month;
            info.release_day = rec.release_day;
            info.wifi_players = rec.wifi_players;
            info.input_players = rec.input_players;
            return info;
        }
    }
    return std::nullopt;
}

} // namespace SwitchFrontend
