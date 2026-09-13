// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/gametdb.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include <curl/curl.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <INIReader.h>

#include "citra_switch/updater.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"

namespace SwitchFrontend {

namespace {

constexpr const char* kUserAgent = "dekopon-gametdb";
constexpr const char* kCaBundlePath = "romfs:/cacert.pem";
constexpr long kConnectTimeoutSeconds = 10;
constexpr long kTransferTimeoutSeconds = 20;
// Longest edge a decoded cover is scaled to before it's uploaded to the GPU atlas. The Game
// Details hero cover renders considerably larger than this cap once used to assume (its right
// column runs several hundred px tall/wide at native output resolution) - 400 keeps decoded
// covers close to GameTDB's own "coverHQ" native size (so this rarely downscales at all) without
// letting a cover eat an unbounded slice of the shared 1024x1024 atlas the glyph/game-icon text
// also lives in (see ui_game_icons.cpp and gpu_canvas.cpp's kDefaultAtlasSize).
constexpr int kMaxEdge = 400;

// Every language folder GameTDB might have 3DS cover art under. US/EN/DE/FR/ES/IT/JA/KO are
// verified to exist empirically (see LanguagesFor below); NL/PT/RU/ZHCN/ZHTW are attempted on the
// same best-effort basis as everything else here - a miss is just a quiet 404, never an error.
constexpr std::array<const char*, 13> kCoverLanguages = {
    "US", "EN", "DE", "FR", "ES", "IT", "JA", "KO", "NL", "PT", "RU", "ZHCN", "ZHTW",
};

std::string SettingsFile() {
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "gametdb_settings.ini";
}

bool s_enabled_loaded = false;
bool s_enabled = false;

void LoadEnabledSetting() {
    s_enabled_loaded = true;
    s_enabled = false;
    const std::string path = SettingsFile();
    if (!FileUtil::Exists(path)) {
        return;
    }
    std::string buffer;
    if (!FileUtil::ReadFileToString(true, path, buffer)) {
        return;
    }
    INIReader ini{buffer.c_str(), buffer.size()};
    if (ini.ParseError() < 0) {
        LOG_ERROR(Config, "Malformed GameTDB settings file '{}'", path);
        return;
    }
    s_enabled = ini.GetBoolean("GameTDB", "enabled", false);
}

std::string CacheDir() {
    return FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + "covers" + DIR_SEP;
}

std::string CoverCachePath(const std::string& game_id, const char* lang) {
    return CacheDir() + game_id + "_" + lang + ".jpg";
}

bool DecodeAndDownscale(const std::vector<u8>& jpeg_bytes, CoverImage& out_image) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(jpeg_bytes.data(), static_cast<int>(jpeg_bytes.size()),
                                             &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return false;
    }
    constexpr int kMaxDecodedEdge = 4096;
    if (width > kMaxDecodedEdge || height > kMaxDecodedEdge) {
        stbi_image_free(decoded);
        return false;
    }

    std::vector<u8> rgba(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(decoded);

    auto w = static_cast<u32>(width);
    auto h = static_cast<u32>(height);
    const int long_edge = static_cast<int>(std::max(w, h));
    if (long_edge > kMaxEdge) {
        const float scale = static_cast<float>(kMaxEdge) / static_cast<float>(long_edge);
        const u32 new_w = std::max<u32>(1, static_cast<u32>(static_cast<float>(w) * scale));
        const u32 new_h = std::max<u32>(1, static_cast<u32>(static_cast<float>(h) * scale));
        std::vector<u8> resized(static_cast<std::size_t>(new_w) * new_h * 4);
        for (u32 y = 0; y < new_h; ++y) {
            const u32 src_y = std::min(h - 1, static_cast<u32>(static_cast<float>(y) / scale));
            for (u32 x = 0; x < new_w; ++x) {
                const u32 src_x = std::min(w - 1, static_cast<u32>(static_cast<float>(x) / scale));
                const std::size_t src_idx = (static_cast<std::size_t>(src_y) * w + src_x) * 4;
                const std::size_t dst_idx = (static_cast<std::size_t>(y) * new_w + x) * 4;
                std::memcpy(&resized[dst_idx], &rgba[src_idx], 4);
            }
        }
        rgba = std::move(resized);
        w = new_w;
        h = new_h;
    }

    out_image.width = static_cast<int>(w);
    out_image.height = static_cast<int>(h);
    out_image.pixels.resize(static_cast<std::size_t>(w) * h);
    std::memcpy(out_image.pixels.data(), rgba.data(), rgba.size());
    return true;
}

// Service::CFG::SystemLanguage (0-11, see settings_model.cpp's LanguageName) -> the GameTDB
// folder(s) most likely to have this title's cover in that language, tried first. Everything in
// kCoverLanguages is tried after these as a fallback, so a title missing art in the preferred
// language still shows whatever language was actually downloaded rather than nothing.
std::vector<const char*> PreferredLanguages(int cfg_language) {
    switch (cfg_language) {
    case 0: // Japanese
        return {"JA"};
    case 1: // English
        return {"EN", "US"};
    case 2: // French
        return {"FR"};
    case 3: // German
        return {"DE"};
    case 4: // Italian
        return {"IT"};
    case 5: // Spanish
        return {"ES"};
    case 6: // Simplified Chinese
        return {"ZHCN"};
    case 7: // Korean
        return {"KO"};
    case 8: // Dutch
        return {"NL"};
    case 9: // Portuguese
        return {"PT"};
    case 10: // Russian
        return {"RU"};
    case 11: // Traditional Chinese
        return {"ZHTW"};
    default:
        return {"EN", "US"};
    }
}

std::size_t CurlWriteToVector(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::vector<u8>*>(userdata);
    const std::size_t bytes = size * nmemb;
    out->insert(out->end(), ptr, ptr + bytes);
    return bytes;
}

// NotFound is GameTDB returning a plain 404 (no body worth keeping) for a language folder that
// doesn't have this title's art - expected, silent, "try the next language", not an error.
// ConnectionFailed means curl couldn't even complete the request (DNS failure, refused/timed-out
// connection, TLS failure, ...) within kConnectTimeoutSeconds/kTransferTimeoutSeconds - unlike a
// 404, this means the network itself isn't working, so retrying the next (game, language)
// combination would almost certainly just fail the same way, ten seconds at a time, for however
// many are left. DownloadWorker treats this as a reason to stop the whole run rather than grind
// through every remaining candidate the same way.
enum class HttpResult { Success, NotFound, ConnectionFailed };

HttpResult HttpGetBinary(const std::string& url, std::vector<u8>& out_body) {
    EnsureCurlInitialized();
    CURL* curl = curl_easy_init();
    if (!curl) {
        return HttpResult::ConnectionFailed;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundlePath);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTransferTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToVector);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);

    const CURLcode res = curl_easy_perform(curl);
    HttpResult result = HttpResult::ConnectionFailed;
    if (res == CURLE_OK) {
        // A 404 (or any other non-200) is still CURLE_OK at this level - curl only reports a
        // non-OK code for a genuine connection/protocol failure, not an HTTP error status.
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        result = (status == 200 && !out_body.empty()) ? HttpResult::Success : HttpResult::NotFound;
    }
    curl_easy_cleanup(curl);
    return result;
}

struct DownloadState {
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> connection_error{false};
    std::atomic<int> games_done{0};
    std::atomic<int> games_total{0};
    std::atomic<int> covers_found{0};
};

DownloadState s_download;
std::thread s_download_thread;

// Guards every touch of the covers cache directory on the SD card - the download worker writes
// to it from a background thread while GetCachedCover() reads from it on the main thread (every
// time a Game Details screen opens, not just around a boot), and true concurrent access from two
// threads isn't something the SD card filesystem layer is guaranteed to handle safely. This is
// what actually stops the crash; StopCoverDownload() at boot (see ui_gamedetails.cpp) is kept as
// well since it also avoids the download competing with BootRom's own heavy SD card I/O for
// bandwidth, but it doesn't cover simply opening Game Details while a download is still running
// in the background, which this does.
std::mutex s_cache_io_mutex;

void DownloadWorker(std::vector<std::string> game_ids) {
    FileUtil::CreateFullPath(CacheDir());
    for (const std::string& game_id : game_ids) {
        for (const char* lang : kCoverLanguages) {
            // Checked between files, never mid-fetch/mid-write - StopCoverDownload() needs this
            // worker to actually stop touching the SD card promptly, not abandon a request or a
            // write partway through (which would just create a new source of corrupt cache files).
            if (s_download.stop_requested) {
                s_download.running = false;
                return;
            }
            const std::string cache_path = CoverCachePath(game_id, lang);
            bool already_exists = false;
            {
                std::lock_guard<std::mutex> lock{s_cache_io_mutex};
                already_exists = FileUtil::Exists(cache_path);
            }
            if (already_exists) {
                s_download.covers_found.fetch_add(1);
                continue;
            }
            const std::string url =
                "https://art.gametdb.com/3ds/coverHQ/" + std::string(lang) + "/" + game_id + ".jpg";
            std::vector<u8> body;
            // The network fetch itself never touches the SD card, so it stays outside the lock -
            // no reason to block GetCachedCover() on the main thread for an entire HTTP round
            // trip just because a download happens to be in flight.
            const HttpResult result = HttpGetBinary(url, body);
            if (result == HttpResult::ConnectionFailed) {
                // No point grinding through every remaining (game, language) pair the same way,
                // each costing another kConnectTimeoutSeconds - stop now and let the UI tell the
                // user, rather than silently taking a very long time to fail one request at a
                // time. Already-cached covers from before the connection dropped are untouched.
                s_download.connection_error = true;
                s_download.running = false;
                return;
            }
            if (result == HttpResult::Success) {
                std::lock_guard<std::mutex> lock{s_cache_io_mutex};
                FileUtil::IOFile out{cache_path, "wb"};
                if (out.IsOpen() && out.WriteBytes(body.data(), body.size()) == body.size()) {
                    s_download.covers_found.fetch_add(1);
                }
            }
        }
        s_download.games_done.fetch_add(1);
    }
    s_download.running = false;
}

} // namespace

bool GetGameTdbEnabled() {
    if (!s_enabled_loaded) {
        LoadEnabledSetting();
    }
    return s_enabled;
}

void SetGameTdbEnabled(bool enabled) {
    s_enabled_loaded = true;
    s_enabled = enabled;

    const std::string path = SettingsFile();
    FileUtil::CreateFullPath(path);
    const std::string contents =
        std::string("[GameTDB]\nenabled = ") + (enabled ? "true" : "false") + "\n";
    if (!FileUtil::WriteStringToFile(true, path, contents)) {
        LOG_ERROR(Config, "Failed to save GameTDB settings to '{}'", path);
    }
}

void StartCoverDownload(const std::vector<GameEntry>& games) {
    if (s_download.running) {
        return;
    }
    std::vector<std::string> game_ids;
    std::unordered_set<std::string> seen;
    for (const GameEntry& entry : games) {
        const std::string game_id = GameTdbGameId(entry.product_code);
        if (game_id.empty() || !seen.insert(game_id).second) {
            continue;
        }
        game_ids.push_back(game_id);
    }
    if (game_ids.empty()) {
        return;
    }

    if (s_download_thread.joinable()) {
        // Only reachable once a prior run has already finished (running gates re-entry above),
        // so this returns immediately - just reclaiming the finished thread's resources.
        s_download_thread.join();
    }

    s_download.games_done = 0;
    s_download.games_total = static_cast<int>(game_ids.size());
    s_download.covers_found = 0;
    s_download.stop_requested = false;
    s_download.connection_error = false;
    s_download.running = true;
    s_download_thread = std::thread(DownloadWorker, std::move(game_ids));
}

bool IsCoverDownloadRunning() {
    return s_download.running;
}

void StopCoverDownload() {
    if (!s_download.running) {
        return;
    }
    s_download.stop_requested = true;
    if (s_download_thread.joinable()) {
        s_download_thread.join();
    }
}

CoverDownloadProgress GetCoverDownloadProgress() {
    CoverDownloadProgress progress;
    progress.games_done = s_download.games_done;
    progress.games_total = s_download.games_total;
    progress.covers_found = s_download.covers_found;
    progress.connection_error = s_download.connection_error;
    return progress;
}

std::optional<CoverImage> GetCachedCover(const std::string& product_code, int cfg_language) {
    const std::string game_id = GameTdbGameId(product_code);
    if (game_id.empty()) {
        return std::nullopt;
    }

    std::vector<const char*> order = PreferredLanguages(cfg_language);
    for (const char* lang : kCoverLanguages) {
        const bool already_queued =
            std::any_of(order.begin(), order.end(),
                       [&](const char* o) { return std::strcmp(o, lang) == 0; });
        if (!already_queued) {
            order.push_back(lang);
        }
    }

    // Held for the whole search, not per-file - the download worker writes one file at a time
    // too (see DownloadWorker), so this is at most a brief wait, never a long one.
    std::lock_guard<std::mutex> lock{s_cache_io_mutex};
    for (const char* lang : order) {
        const std::string cache_path = CoverCachePath(game_id, lang);
        std::string jpeg_bytes;
        if (!FileUtil::Exists(cache_path) ||
            !FileUtil::ReadFileToString(true, cache_path, jpeg_bytes) || jpeg_bytes.empty()) {
            continue;
        }
        CoverImage image;
        if (DecodeAndDownscale(std::vector<u8>(jpeg_bytes.begin(), jpeg_bytes.end()), image)) {
            return image;
        }
    }
    return std::nullopt;
}

} // namespace SwitchFrontend
