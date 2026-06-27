// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Facade between the <switch.h> main() and the Azahar core.
namespace SwitchFrontend {

// Sets up default directory and logging on first boot.
int Bootstrap();

// Flushes and stops the logger.
void Shutdown();

// Brings up the EGL/GLES EmuWindow on the given libnx nwindow.
bool CreateWindow(void* native_window);

// Clear the window and present one frame for testing.
void PresentFrame();

// Tears down the EmuWindow and its GL context.
void DestroyWindow();

} // namespace SwitchFrontend
