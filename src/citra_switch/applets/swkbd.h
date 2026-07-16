// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core {
class System;
}

namespace SwitchFrontend {

// Routes the guest's software keyboard to the Horizon swkbd library applet.

/// Registers the keyboard and drops any request left over from a previous session. Call before
/// booting a ROM.
void RegisterKeyboard(Core::System& system);

/// Launches the system keyboard if the emulation thread asked for one, and blocks for as long as it
/// is up. Must be called from the thread that owns appletMainLoop().
void PumpKeyboard();

/// Releases an emulation thread blocked waiting for a keyboard, so shutdown cannot deadlock on one
/// nobody is going to pump. Call from the same thread as PumpKeyboard().
void CancelKeyboard();

} // namespace SwitchFrontend
