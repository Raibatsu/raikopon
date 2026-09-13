// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/whats_next.h"

#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>

#include <curl/curl.h>

#include "citra_switch/updater.h"
#include "common/file_util.h"
#include "common/string_util.h"

namespace SwitchFrontend {

namespace {

constexpr const char* kFetchUrl =
    "https://raw.githubusercontent.com/Raibatsu/raikopon/master/To%20Do.txt";
constexpr const char* kUserAgent = "raikopon-updater";
constexpr const char* kCaBundlePath = "romfs:/cacert.pem";
constexpr long kConnectTimeoutSeconds = 15;
constexpr long kTransferTimeoutSeconds = 30;

std::string CacheFile() {
    return FileUtil::GetUserPath(FileUtil::UserPath::CacheDir) + "whats_next.txt";
}

std::size_t CurlWriteToString(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

bool FetchWhatsNext(std::string& out_body) {
    EnsureCurlInitialized();
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, kFetchUrl);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundlePath);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTransferTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_body);

    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_easy_cleanup(curl);
    return res == CURLE_OK && status >= 200 && status < 300;
}

std::vector<WhatsNextItem> ParseWhatsNext(const std::string& body) {
    std::vector<WhatsNextItem> items;
    for (const std::string& raw_line : Common::SplitString(body, '\n')) {
        std::string line = Common::StripSpaces(raw_line);
        if (!line.empty() && (line.front() == '*' || line.front() == '-')) {
            line.erase(0, 1);
            line = Common::StripSpaces(line);
        }
        if (line.empty()) {
            continue;
        }
        WhatsNextItem item;
        if (line.size() >= 4 && line.substr(0, 2) == "~~" && line.substr(line.size() - 2) == "~~") {
            item.done = true;
            item.text = line.substr(2, line.size() - 4);
        } else {
            item.text = line;
        }
        if (!item.text.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

std::atomic<bool> s_fetch_started{false};
std::atomic<bool> s_fetch_ready{false};
std::thread s_fetch_thread;
std::string s_fetch_body;
bool s_fetch_ok = false;

bool s_attempted = false;
bool s_ready = false;
std::vector<WhatsNextItem> s_items;

} // namespace

void EnsureWhatsNextLoaded() {
    if (s_attempted) {
        return;
    }

    if (s_fetch_started.load(std::memory_order_acquire)) {
        if (!s_fetch_ready.load(std::memory_order_acquire)) {
            return;
        }
        s_fetch_thread.join();
        s_attempted = true;
        if (s_fetch_ok) {
            const std::string cache_path = CacheFile();
            FileUtil::CreateFullPath(cache_path);
            FileUtil::WriteStringToFile(true, cache_path, s_fetch_body);
            s_items = ParseWhatsNext(s_fetch_body);
            s_ready = true;
        }
        return;
    }

    std::string cached;
    if (FileUtil::ReadFileToString(true, CacheFile(), cached)) {
        s_items = ParseWhatsNext(cached);
        s_ready = true;
        s_attempted = true;
        return;
    }

    s_fetch_started.store(true, std::memory_order_release);
    s_fetch_thread = std::thread([] {
        s_fetch_ok = FetchWhatsNext(s_fetch_body);
        s_fetch_ready.store(true, std::memory_order_release);
    });
}

bool IsWhatsNextReady() {
    return s_ready;
}

const std::vector<WhatsNextItem>& WhatsNextItems() {
    return s_items;
}

} // namespace SwitchFrontend
