// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string_view>

#include <curl/curl.h>
#include <json.hpp>
#include <mbedtls/sha256.h>

#include "citra_switch/config.h"
#include "citra_switch/updater.h"
#include "common/file_util.h"
#include "raikopon_version.h"

namespace SwitchFrontend {

namespace {

constexpr const char* kRepoOwner = "Raibatsu";
constexpr const char* kRepoName = "raikopon";
constexpr const char* kNroAssetName = "raikopon.nro";
constexpr const char* kChecksumAssetName = "raikopon.nro.sha256";
constexpr const char* kUserAgent = "raikopon-updater";
// switch-curl links against switch-mbedtls, which has no OS trust-store access - see the comment
// on the romfs packaging in CMakeLists.txt for where this file comes from.
constexpr const char* kCaBundlePath = "romfs:/cacert.pem";
constexpr long kConnectTimeoutSeconds = 15;
// Whole-transfer cap. The API/checksum requests are tiny and finish in well under this; the .nro
// download is the one that could plausibly need most of it on a slow connection.
constexpr long kTransferTimeoutSeconds = 180;

std::string s_own_nro_path;
std::once_flag s_curl_init_once;

void EnsureCurlInitialized() {
    std::call_once(s_curl_init_once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct ParsedVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string suffix; // Empty means no prerelease suffix (higher precedence than any suffix).
    bool valid = false;
};

// Parses "v1.6.0-beta.1" style tags: optional leading v/V, up to 3 dot-separated numeric
// segments, optional "-suffix". Structurally malformed input (non-numeric segments, no numeric
// segments at all) leaves `valid` false - callers must never guess in that case.
ParsedVersion ParseVersion(std::string_view tag) {
    ParsedVersion result;
    if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
        tag.remove_prefix(1);
    }
    if (tag.empty()) {
        return result;
    }

    std::string_view numeric = tag;
    const auto dash = tag.find('-');
    if (dash != std::string_view::npos) {
        numeric = tag.substr(0, dash);
        result.suffix = std::string(tag.substr(dash + 1));
    }
    if (numeric.empty()) {
        return result;
    }

    std::array<int, 3> parts{0, 0, 0};
    std::size_t part_index = 0;
    std::size_t pos = 0;
    while (part_index < 3) {
        const auto dot = numeric.find('.', pos);
        const std::string_view segment = numeric.substr(
            pos, dot == std::string_view::npos ? std::string_view::npos : dot - pos);
        if (segment.empty() || !std::all_of(segment.begin(), segment.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            })) {
            return result; // Non-numeric or empty segment: structurally invalid.
        }
        parts[part_index++] = std::atoi(std::string(segment).c_str());
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }

    result.major = parts[0];
    result.minor = parts[1];
    result.patch = parts[2];
    result.valid = true;
    return result;
}

// True if `candidate` should be offered as an update over `current`. Same-number releases beat
// same-number prereleases; among two suffixed versions, plain string comparison is good enough
// (this project doesn't need full SemVer prerelease-precedence rules, just "never go backwards").
bool IsNewer(const ParsedVersion& candidate, const ParsedVersion& current) {
    if (!candidate.valid || !current.valid) {
        return false;
    }
    if (candidate.major != current.major) {
        return candidate.major > current.major;
    }
    if (candidate.minor != current.minor) {
        return candidate.minor > current.minor;
    }
    if (candidate.patch != current.patch) {
        return candidate.patch > current.patch;
    }
    if (candidate.suffix.empty() != current.suffix.empty()) {
        return candidate.suffix.empty();
    }
    return candidate.suffix > current.suffix;
}

std::string HexEncode(const unsigned char* bytes, std::size_t size) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kHexDigits[bytes[i] >> 4]);
        out.push_back(kHexDigits[bytes[i] & 0xF]);
    }
    return out;
}

std::size_t CurlWriteToString(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Common options shared by every request this module makes (auth-less GitHub GETs and the small
// checksum sidecar download all go through this; the big .nro download sets up its own handle
// separately since it streams to a file and reports progress instead).
void ApplyCommonOptions(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundlePath);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTransferTimeoutSeconds);
    // The Switch's BSD sockets service doesn't reliably support IPv6; without this, curl's
    // default dual-stack resolution can stall on an AAAA lookup/connect attempt before ever
    // falling back to IPv4, which looks identical to "no network" from the caller's side.
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
}

// `out_error`, if non-null, is set to a short diagnostic (curl's own error string, or "HTTP
// <status>") whenever this returns false or the caller rejects out_status. Purely for surfacing
// in the UI so a real device test tells us DNS/TLS/timeout vs. a plain 404/403 instead of every
// failure looking like generic "network error".
bool HttpGetString(const std::string& url, bool github_api, std::string& out_body,
                   long& out_status, std::string* out_error = nullptr) {
    EnsureCurlInitialized();
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (out_error) {
            *out_error = "curl_easy_init failed";
        }
        return false;
    }
    curl_slist* headers = nullptr;
    if (github_api) {
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    }

    ApplyCommonOptions(curl, url);
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);

    const CURLcode res = curl_easy_perform(curl);
    const bool ok = res == CURLE_OK;
    if (ok) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out_status);
    } else if (out_error) {
        *out_error = curl_easy_strerror(res);
    }
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    return ok;
}

struct ReleaseAssets {
    bool nro_found = false;
    bool sha256_found = false;
    std::string nro_url;
    std::string sha256_url;
    std::uint64_t nro_size = 0;
};

ReleaseAssets ExtractAssets(const nlohmann::json& release) {
    ReleaseAssets out;
    if (!release.contains("assets") || !release["assets"].is_array()) {
        return out;
    }
    for (const auto& asset : release["assets"]) {
        if (!asset.contains("name") || !asset["name"].is_string() ||
            !asset.contains("browser_download_url") ||
            !asset["browser_download_url"].is_string()) {
            continue;
        }
        const std::string name = asset["name"].get<std::string>();
        if (name == kNroAssetName) {
            out.nro_url = asset["browser_download_url"].get<std::string>();
            out.nro_size = asset.value("size", static_cast<std::uint64_t>(0));
            out.nro_found = true;
        } else if (name == kChecksumAssetName) {
            out.sha256_url = asset["browser_download_url"].get<std::string>();
            out.sha256_found = true;
        }
    }
    return out;
}

struct FileWriteContext {
    FileUtil::IOFile* file = nullptr;
};

std::size_t CurlWriteToFile(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* ctx = static_cast<FileWriteContext*>(userdata);
    const std::size_t total = size * nmemb;
    return ctx->file->WriteBytes(ptr, total);
}

struct ProgressContext {
    const std::function<void(std::size_t, std::size_t)>* callback;
};

int CurlXferInfo(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t /*ultotal*/,
                 curl_off_t /*ulnow*/) {
    const auto* ctx = static_cast<ProgressContext*>(clientp);
    if (ctx->callback && *ctx->callback) {
        (*ctx->callback)(static_cast<std::size_t>(dlnow), static_cast<std::size_t>(dltotal));
    }
    return 0; // Non-zero would abort the transfer.
}

} // namespace

void SetOwnNroPath(const std::string& path) {
    s_own_nro_path = path;
}

bool HasOwnNroPath() {
    return !s_own_nro_path.empty();
}

const std::string& GetOwnNroPath() {
    return s_own_nro_path;
}

UpdateCheckOutcome CheckForUpdate(UpdateChannel channel) {
    UpdateCheckOutcome outcome;

    std::string url =
        std::string("https://api.github.com/repos/") + kRepoOwner + "/" + kRepoName;
    url += channel == UpdateChannel::Stable ? "/releases/latest" : "/releases";

    std::string body;
    long status = 0;
    std::string error;
    const bool transport_ok = HttpGetString(url, true, body, status, &error);
    if (!transport_ok || status < 200 || status >= 300) {
        outcome.result = UpdateCheckResult::NetworkError;
        outcome.diagnostic = transport_ok ? "HTTP " + std::to_string(status) : error;
        return outcome;
    }

    try {
        const nlohmann::json parsed = nlohmann::json::parse(body);
        nlohmann::json release;
        if (channel == UpdateChannel::Stable) {
            release = parsed;
        } else {
            if (!parsed.is_array()) {
                outcome.result = UpdateCheckResult::ParseError;
                return outcome;
            }
            bool found = false;
            for (const auto& entry : parsed) {
                if (entry.value("prerelease", false)) {
                    release = entry;
                    found = true;
                    break;
                }
            }
            if (!found) {
                outcome.result = UpdateCheckResult::NoReleaseFound;
                return outcome;
            }
        }

        if (!release.contains("tag_name") || !release["tag_name"].is_string()) {
            outcome.result = UpdateCheckResult::ParseError;
            return outcome;
        }
        const std::string tag = release["tag_name"].get<std::string>();

        const ReleaseAssets assets = ExtractAssets(release);
        if (!assets.nro_found || !assets.sha256_found) {
            // A release missing either asset is invisible to the updater - safe failure mode,
            // see CMakeLists.txt/plan notes on the release-process requirement.
            outcome.result = UpdateCheckResult::NoReleaseFound;
            return outcome;
        }

        const ParsedVersion candidate = ParseVersion(tag);
        const ParsedVersion current = ParseVersion(kRaikoponVersion);
        if (!candidate.valid || !current.valid) {
            outcome.result = UpdateCheckResult::ParseError;
            return outcome;
        }

        if (!IsNewer(candidate, current)) {
            outcome.result = UpdateCheckResult::UpToDate;
            return outcome;
        }

        outcome.result = UpdateCheckResult::UpdateAvailable;
        outcome.info.tag_name = tag;
        outcome.info.nro_download_url = assets.nro_url;
        outcome.info.sha256_download_url = assets.sha256_url;
        outcome.info.nro_size = assets.nro_size;
        return outcome;
    } catch (const nlohmann::json::exception&) {
        outcome.result = UpdateCheckResult::ParseError;
        return outcome;
    }
}

DownloadResult DownloadAndInstallUpdate(
    const UpdateInfo& info, const std::function<void(std::size_t, std::size_t)>& progress) {
    if (!HasOwnNroPath()) {
        return DownloadResult::ReplaceFailed;
    }
    EnsureCurlInitialized();

    const std::string_view parent = FileUtil::GetParentPath(s_own_nro_path);
    const std::string temp_path = std::string(parent) + "/raikopon.nro.download";

    // 1. Download the .nro fully into the temp file (same directory as the live .nro, so the
    //    replace below is an atomic same-filesystem rename, not a copy+delete).
    {
        FileUtil::IOFile out(temp_path, "wb");
        if (!out.IsOpen()) {
            return DownloadResult::NetworkError;
        }
        FileWriteContext write_ctx{&out};
        ProgressContext progress_ctx{&progress};

        CURL* curl = curl_easy_init();
        if (!curl) {
            out.Close();
            FileUtil::Delete(temp_path);
            return DownloadResult::NetworkError;
        }
        ApplyCommonOptions(curl, info.nro_download_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlXferInfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_ctx);

        const CURLcode res = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        out.Close();

        if (res != CURLE_OK || status < 200 || status >= 300) {
            FileUtil::Delete(temp_path);
            return DownloadResult::NetworkError;
        }
    }

    // 2. Download the checksum sidecar (a few dozen bytes - sha256sum format is
    //    "<hex>  <filename>", so take the first whitespace-delimited token).
    std::string checksum_body;
    long checksum_status = 0;
    if (!HttpGetString(info.sha256_download_url, false, checksum_body, checksum_status) ||
        checksum_status < 200 || checksum_status >= 300) {
        FileUtil::Delete(temp_path);
        return DownloadResult::NetworkError;
    }
    const auto space = checksum_body.find_first_of(" \t\r\n");
    std::string expected_hex = checksum_body.substr(0, space);
    std::transform(expected_hex.begin(), expected_hex.end(), expected_hex.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // 3. Stream-hash the downloaded file and compare.
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts_ret(&sha_ctx, 0);
    {
        FileUtil::IOFile in(temp_path, "rb");
        if (!in.IsOpen()) {
            mbedtls_sha256_free(&sha_ctx);
            FileUtil::Delete(temp_path);
            return DownloadResult::NetworkError;
        }
        std::array<unsigned char, 64 * 1024> buffer;
        std::size_t read_count;
        while ((read_count = in.ReadBytes(buffer.data(), buffer.size())) > 0) {
            mbedtls_sha256_update_ret(&sha_ctx, buffer.data(), read_count);
        }
    }
    std::array<unsigned char, 32> digest;
    mbedtls_sha256_finish_ret(&sha_ctx, digest.data());
    mbedtls_sha256_free(&sha_ctx);

    const std::string actual_hex = HexEncode(digest.data(), digest.size());
    if (actual_hex != expected_hex) {
        FileUtil::Delete(temp_path);
        return DownloadResult::ChecksumMismatch;
    }

    // 4. Verified - atomic replace of the live .nro.
    if (!FileUtil::Rename(temp_path, s_own_nro_path)) {
        FileUtil::Delete(temp_path);
        return DownloadResult::ReplaceFailed;
    }
    return DownloadResult::Success;
}

// RelaunchSelf() is implemented in citra_switch.cpp - see updater.h's file comment for why.

} // namespace SwitchFrontend
