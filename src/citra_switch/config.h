// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>

// Facade between the <switch.h> main() and the Azahar core.
namespace SwitchFrontend {

// Directories the frontend owns. Absolute SD paths including a trailing '/'.
struct SwitchPaths {
    std::string user_dir;  // Holds config/, nand/, sdmc/, log/, ...
    std::string roms_dir;  // Scanned for titles.
    bool scan_recursive{}; // Whether the scan descends into roms_dir's subfolders.
};

// Sets up default directory and logging on first boot.
int Bootstrap();

// The configured paths.
const SwitchPaths& GetPaths();

// Persists `paths`. roms_dir and scan_recursive apply to the next scan.
void SetPaths(const SwitchPaths& paths);

// The dekopon directory this session actually booted from.
const std::string& GetActiveUserDir();

// The built-in locations offered as a reset target in the UI.
std::string GetDefaultUserDir();
std::string GetDefaultRomsDir(const std::string& user_dir);

// Serialises the current Settings::values back to config.ini.
void SaveConfig();

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

// Advances the screen layout to the next preset while a game runs.
void CycleScreenLayout();

void MirrorScreenSides();

// Steps the screen layout by `delta` presets and applies it live.
void StepScreenLayout(int delta);

// The name of the currently selected screen layout preset.
const char* CurrentScreenLayoutName();

// The number of screen layout presets R3 and the quick menu can select.
int GetScreenLayoutCount();

// The display name of preset `index` or "" if out of range.
const char* GetScreenLayoutName(int index);

// Bitmask of presets included in R3's cycle (bit i = preset i).
std::uint32_t GetLayoutCycleMask();
void SetLayoutCycleMask(std::uint32_t mask);

// Percentage the Core Clock is throttled to while a movie-library CRO is loaded (see
// core/hle/service/ldr_ro/ldr_ro.cpp). Adjustable from the quick menu; clamped to [10, 100].
std::int32_t GetMovieThrottleClockPercentage();
void SetMovieThrottleClockPercentage(std::int32_t percentage);

// True if the most recent BootRom never reached a successful system.Load.
bool LoadFailed();

// Signals the emulation thread to stop.
void StopRom();

// Stops calling RunLoop() until ResumeEmulation() is called, and mutes audio so the DSP's
// stuck-last-sample FIFO-underrun behaviour isn't audible. No-op if not running or already paused.
void PauseEmulation();

// Resumes RunLoop() after a PauseEmulation() call, restoring audio if PauseEmulation() was the
// one that muted it. No-op if not paused.
void ResumeEmulation();

// True while emulation is paused via PauseEmulation().
bool IsPaused();

} // namespace SwitchFrontend
