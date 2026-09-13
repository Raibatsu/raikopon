// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "citra_switch/glyph_atlas.h"
#include "citra_switch/ui_model.h"
#include "citra_switch/updater.h"

namespace SwitchFrontend {

// The Updates tab's content area (rail + Settings tab strip are already drawn by the caller).
void UpdateUpdatesTab(UiModel& model, const MenuInput& input, GpuCanvas& canvas, GlyphAtlas& atlas,
                     float dt, float content_x, float content_top, float content_w,
                     float viewport_bottom);

// "Auto-check for updates" toggle. Persisted independently of config.ini (its own small file,
// same self-contained FileUtil+INIReader pattern ui_input_bindings.cpp uses for menu bindings) -
// deliberately not routed through config.cpp, which owns unrelated and much higher-risk
// GPU/emulation-lifecycle code that has no reason to be touched for one boolean.
bool GetAutoCheckUpdatesEnabled();
void SetAutoCheckUpdatesEnabled(bool enabled);

// Starts a background update check (updater.h's CheckForUpdate is documented as blocking and
// must run off the UI thread). No-op if one is already running. Used by both the tab's "Check
// Now" row and citra_switch.cpp's boot-time auto-check.
void StartUpdateCheck(UpdateChannel channel);
bool IsUpdateCheckPending();

// True exactly once per StartUpdateCheck() call, the first frame its result becomes available -
// for a caller that only needs to react to a fresh result once (the boot-time notice banner).
bool ConsumeFreshUpdateCheckResult(UpdateCheckOutcome& outcome);

// The most recent check's result, if any, kept around so the tab can keep displaying it across
// frames (unlike ConsumeFreshUpdateCheckResult(), this doesn't clear after one read).
bool HasUpdateCheckResult();
const UpdateCheckOutcome& LastUpdateCheckResult();

void RunUpdateInstall(UiModel& model, GpuCanvas& canvas, GlyphAtlas& atlas, const UpdateInfo& info);
void DrawUpdateReadyModal(GpuCanvas& canvas, GlyphAtlas& atlas);
void DrawChangelogPopup(UiModel& model, const MenuInput& input, GpuCanvas& canvas,
                        GlyphAtlas& atlas);

} // namespace SwitchFrontend
