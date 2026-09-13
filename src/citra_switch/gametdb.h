// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "citra_switch/menu_data.h"

// Downloads 3DS cover art from GameTDB (https://art.gametdb.com/3ds/coverHQ/<lang>/<gameid>.jpg,
// keyed by the cart/CIA's product code) and caches the raw JPEG on the SD card, one file per
// (game, language) pair GameTDB actually has art for. Unlike the old per-view fetch-on-demand
// design, covers are only ever downloaded in one explicit batch (StartCoverDownload, normally
// wired to a Settings button) - browsing the library never triggers network traffic. Opt-in
// display - see Get/SetGameTdbEnabled - since this is the only frontend feature that talks to a
// third party other than GitHub (the updater).
namespace SwitchFrontend {

bool GetGameTdbEnabled();
void SetGameTdbEnabled(bool enabled);

// Kicks off a background download of every owned game's cover, trying every language GameTDB
// might have art in for each one (see kCoverLanguages in gametdb.cpp) and keeping whichever
// succeed - a 404 for a language a title has no art in is expected and silently skipped, never
// treated as an error. No-op if a download is already running. Games already fully cached
// (every language already tried) are still re-checked but cost only a handful of quick misses,
// not a re-download of what's already on disk (existing cache files are never re-fetched).
void StartCoverDownload(const std::vector<GameEntry>& games);

bool IsCoverDownloadRunning();

// Signals the download worker to stop at its next safe point (between one file's HTTP fetch and
// the next, never mid-request) and blocks until it actually exits. Call this before booting a
// game - the worker thread does its own SD card I/O (fetch response bodies, decoded cover
// writes) completely independently of the main thread, which also does heavy SD card I/O while
// loading a ROM, and letting both touch the filesystem at once produced hard-to-reproduce crashes
// during boot. A no-op (returns immediately) if nothing is running. Safe to call even if the
// download is meant to continue later - just call StartCoverDownload again, which resumes from
// wherever the cache already stands (already-downloaded files are never re-fetched).
void StopCoverDownload();

struct CoverDownloadProgress {
    int games_done = 0;
    int games_total = 0;
    int covers_found = 0;
    // True if the run stopped early because a request couldn't even connect (not just a 404) -
    // almost always means the console lost its internet connection partway through. Distinct
    // from just "not running", which also covers a clean, fully-finished run.
    bool connection_error = false;
};
CoverDownloadProgress GetCoverDownloadProgress();

// Decoded cover art, RGBA8888, owned by the caller (unlike the old design, this isn't cached in
// memory beyond the call - GetCoverRegion() in ui_game_icons.cpp is what keeps a decoded copy
// alive, in the shared GPU atlas).
struct CoverImage {
    std::vector<std::uint32_t> pixels;
    int width = 0;
    int height = 0;
};

// Reads and decodes whichever cached cover best matches `cfg_language` (a 3DS
// Service::CFG::SystemLanguage value, 0-11 - see settings_model.cpp's LanguageName) for this
// product code, preferring an exact language match but falling back through the rest of
// kCoverLanguages so any cover that was actually downloaded is still shown rather than nothing.
// std::nullopt if nothing is cached for this title in any language, or decoding fails. Pure
// on-disk-cache lookup - never touches the network (see StartCoverDownload for that).
std::optional<CoverImage> GetCachedCover(const std::string& product_code, int cfg_language);

} // namespace SwitchFrontend
