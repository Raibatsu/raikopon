// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// Facade between the <switch.h> main() and the Azahar core.
namespace SwitchFrontend {

// Sets up default directory and logging on first boot.
int Bootstrap();

// Flushes and stops the logger.
void Shutdown();

// Brings up the EGL/GLES EmuWindow on the given libnx nwindow.
bool CreateWindow(void* native_window);

// Clears the window to a solid colour and presents.
// This will be removed in the future once a UI is established
void ClearFrame();

// Tears down the EmuWindow and its GL context.
void DestroyWindow();

// Resolves a ROM and starts it
bool BootRom(const std::string& rom_arg);

// True while the emulation thread is running.
bool IsRunning();

// Signals the emulation thread to stop.
void StopRom();

} // namespace SwitchFrontend
