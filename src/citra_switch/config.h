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

// Re-points the preset index at whichever preset matches the live settings, so the name the menus
// report stays truthful after something outside the stepper changed the layout (e.g. the touch
// layout editor selecting Custom).
void SyncScreenLayoutIndex();

// Defers a framebuffer relayout to EmuThread's next loop iteration instead of applying it
// immediately. With the async GPU thread, the renderer's framebuffer layout must only be touched
// from the thread that owns it (EmuThread when async GPU is on); callers on the input/frontend
// thread (e.g. the touch layout editor) that used to call
// GPU().Renderer().UpdateCurrentFramebufferLayout() directly must go through this instead.
void RequestFramebufferRelayout();

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

// Pauses emulation and releases the nwindow from the Vulkan swapchain so the caller can draw to
// it with a libnx framebuffer. Returns false if no game is running. The libnx half has to live in
// a translation unit that includes <switch.h>, which core's headers can't coexist with.
bool ReleaseWindowForMenu();

// Undoes ReleaseWindowForMenu: recreates the surface/swapchain and resumes emulation.
void ReclaimWindowFromMenu();

// TEMPORARY. Logs `message` via Common::Log and flushes synchronously before returning. Exists so
// citra_switch.cpp's nwindow probe (which can't include common/logging/log.h - see the comment at
// its call site) can still get a checkpoint into azahar_log.txt that survives a crash on the very
// next line.
void ProbeLog(const char* message);

} // namespace SwitchFrontend
