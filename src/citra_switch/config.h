// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Bring-up bootstrap for the Switch frontend.
namespace SwitchFrontend {

// Sets up default directory and logging on first boot.
int Bootstrap();

// Flushes and stops the logger.
void Shutdown();

} // namespace SwitchFrontend
