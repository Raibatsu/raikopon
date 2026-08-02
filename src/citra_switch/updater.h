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
    // Reserved: no current path returns this (the old "rename straight onto the live .nro" step
    // this used to describe was removed - see GetUpdateStagingPath()'s comment for why - and
    // nothing since replaces it with a comparable synchronous failure). Kept so DownloadResult's
    // set of values doesn't shift under any external code and switches over it stay exhaustive.
    ReplaceFailed,
};

// Downloads info.nro_download_url to GetUpdateStagingPath(), verifies it against
// info.sha256_download_url's content, and leaves it there (does NOT touch the live .nro - call
// FinishInstall() for that once the user confirms). `progress` is invoked with (bytes_so_far,
// total_bytes) from the calling (worker) thread. On any failure the staged file is deleted before
// returning - there is no resume; the caller must restart the whole check-then-download flow from
// scratch. Blocking - run on a worker thread.
DownloadResult DownloadAndInstallUpdate(
    const UpdateInfo& info, const std::function<void(std::size_t, std::size_t)>& progress);

// Where DownloadAndInstallUpdate() stages a verified download - <dir>/raikopon.nro.new, same
// directory as the live .nro (required for FileUtil::Rename's same-filesystem guarantee). A fixed
// name, not one unique per download cycle: an earlier version of this code used a
// timestamp-suffixed name (and a whole separate staging-boot/relaunch dance around it) to work
// around what looked like Horizon refusing to replace the file the current process is running
// from - that turned out to be a misdiagnosis (see FinishInstall()'s comment for what was actually
// going on), so there's no reason left to avoid a fixed name.
std::string GetUpdateStagingPath();

// Renames GetOwnNroPath() aside and renames GetUpdateStagingPath() onto it - i.e. actually
// installs a download that DownloadAndInstallUpdate() already verified. The caller MUST call
// UnmountRomfsForSelfReplace() first (see its comment for why - hardware-confirmed, not optional).
// Safe to call while this process is still running from GetOwnNroPath(): Horizon loads an NRO's
// contents into memory once at process start and never re-reads the file afterward, so overwriting
// the file on disk doesn't touch the already-running in-memory code - only the *next* process to
// load that path (this same one, immediately after, via RelaunchSelf()) sees the new bytes. Pure
// file I/O, safe to call from the core side (no libnx dependency). Returns false on I/O failure
// (destination still has the old binary, restored from the backup if the second rename is what
// failed; the staged .new file is left alone so the user can retry from the Updates tab without
// re-downloading).
//
// The caller relaunches immediately after a successful call here - RelaunchSelf(), not a second
// hop into some intermediate path. There's nothing left to reload except the file that was just
// written.
bool FinishInstall();

// Unmounts the embedded romfs partition before FinishInstall() touches the live .nro. Must be
// called first - hardware-confirmed: romfs is mounted directly out of the running .nro's own
// embedded asset section, and leaving it mounted makes even a plain rename() of that file fail
// with errno 5 (I/O error). Implemented in citra_switch.cpp (needs libnx's romfsExit()).
void UnmountRomfsForSelfReplace();

// Only needed if FinishInstall() fails after UnmountRomfsForSelfReplace() already ran - remounts
// romfs so the still-running process (no relaunch happens on a failed install) keeps working HTTPS
// (the CA bundle lives in romfs) for a retry.
void RemountRomfsAfterFailedSelfReplace();

// Ends the process and asks hbloader to load `path` next. `path` must end in .nro - libnx's
// envSetNextLoad() docs say so explicitly. An empty path means "just exit, don't load anything" -
// used for a plain return to whatever launched this process. Never returns on success (aborts
// instead if HasOwnNroPath() is false when a non-empty path is used - RelaunchSelf() below only
// ever reaches that state via GetOwnNroPath(), which already requires HasOwnNroPath()).
// Implemented in citra_switch.cpp - see the file comment above for why.
[[noreturn]] void RelaunchInto(const std::string& path);

// Equivalent to RelaunchInto(GetOwnNroPath()) - reloads the current .nro from disk. Used both
// after a successful FinishInstall() (to boot into the just-installed update) and for every other
// restart-required case (a settings change, the dekopon folder having moved, ...).
[[noreturn]] void RelaunchSelf();

} // namespace SwitchFrontend
