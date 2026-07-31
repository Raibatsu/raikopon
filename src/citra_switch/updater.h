// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "citra_switch/config.h"

// GitHub-release-based in-app updater. Stable tracks GitHub releases (prerelease=false, fetched
// via /releases/latest); Experimental tracks the newest GitHub pre-release. A release is only
// ever offered if its version is strictly newer than the running build's, regardless of channel.
//
// updater.cpp itself is core-side only (needs common/file_util.h, which pulls in
// common/common_types.h - the same <switch.h>-vs-core-headers u128 conflict described in
// emulation.cpp's ReleaseWindowForMenu comment). RelaunchSelf() is the one piece of this API that
// needs libnx's envSetNextLoad, so - same split as ingame_settings.cpp/ingame_cheats.cpp - its
// implementation lives in citra_switch.cpp instead, reading the path back via GetOwnNroPath().
// This header itself stays plain-C++ so it's safe to include from either side.
namespace SwitchFrontend {

// Must be called once, at the very top of main(), with argv[0] before anything else here is used.
// If path is empty, HasOwnNroPath() returns false and every entry point below refuses rather than
// guessing a fallback location to read/write.
void SetOwnNroPath(const std::string& path);
bool HasOwnNroPath();
const std::string& GetOwnNroPath();

enum class UpdateCheckResult {
    UpdateAvailable,
    UpToDate,
    NoReleaseFound, // Channel has no release with both a .nro and a .sha256 asset.
    NetworkError,
    ParseError, // Malformed API response or version tag; treated as "no update", never guessed.
};

struct UpdateInfo {
    std::string tag_name; // Raw GitHub tag, e.g. "v1.6.0-beta.1".
    std::string nro_download_url;
    std::string sha256_download_url;
    std::uint64_t nro_size{};
};

struct UpdateCheckOutcome {
    UpdateCheckResult result{};
    UpdateInfo info{};       // Only meaningful when result == UpdateAvailable.
    std::string diagnostic;  // Extra detail for NetworkError/ParseError (curl error or HTTP
                              // status) — empty otherwise. Not meant to be user-friendly prose,
                              // just enough to tell a DNS/TLS failure from a 404/403 at a glance.
};

// Blocking network call - run on a worker thread, never the UI thread.
UpdateCheckOutcome CheckForUpdate(UpdateChannel channel);

enum class DownloadResult {
    Success,
    NetworkError,
    ChecksumMismatch,
    ReplaceFailed, // Verified, but the final rename onto the live .nro failed.
};

// Downloads info.nro_download_url to a temp file next to the running .nro, verifies it against
// info.sha256_download_url's content, and only on success atomically replaces the running .nro.
// `progress` is invoked with (bytes_so_far, total_bytes) from the calling (worker) thread. On any
// failure the temp file is deleted before returning - there is no resume; the caller must restart
// the whole check-then-download flow from scratch. Blocking - run on a worker thread.
DownloadResult DownloadAndInstallUpdate(
    const UpdateInfo& info, const std::function<void(std::size_t, std::size_t)>& progress);

// Ends the process and asks hbloader to load the just-replaced .nro next. Never returns on
// success (aborts instead if HasOwnNroPath() is false). Only call this after a successful
// DownloadAndInstallUpdate, once the user has dismissed the "update installed" prompt.
// Implemented in citra_switch.cpp - see the file comment above for why.
[[noreturn]] void RelaunchSelf();

} // namespace SwitchFrontend
