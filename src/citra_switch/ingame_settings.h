// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

namespace SwitchFrontend {

// Full-screen, native-libnx-framebuffer settings + cheats screen shown over a paused game via the
// nwindow handoff (ReleaseWindowForMenu/ReclaimWindowFromMenu). Drives the exact same row model
// (settings_model.h) the library's Settings tab uses, so there's one definition of what a
// setting row is — only the persistence differs (per-game override here, global config there).
// Blocks until the player closes the screen; safe to call even if no game is running (no-op).
// Returns true if the player confirmed "Quit Game" (Plus, then confirm) — the caller should stop
// the emulation and return to the library; false means the screen just closed normally.
bool ShowInGameSettings(PadState& pad);

} // namespace SwitchFrontend
