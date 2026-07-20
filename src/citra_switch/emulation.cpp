// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#include "citra_switch/applets/swkbd.h"
#include "citra_switch/config.h"
#include "citra_switch/emu_window.h"
#include "common/file_util.h"
#include "common/horizon_thread.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/loader/loader.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

namespace SwitchFrontend {

namespace {

std::thread s_emu_thread;
std::atomic<bool> s_stop{true};
// Set true once system.Load succeeds.
// This lets the menu tell a crash/bad ROM apart from a clean exit.
std::atomic<bool> s_load_ok{false};

// Guards the pause-wait handshake below — closes the lost-wakeup window between a waiter
// checking the predicate and actually going to sleep. Does not "protect" s_paused's value
// (that's atomic); it exists purely for the wait/notify pairing.
std::mutex s_pause_mutex;
std::condition_variable s_pause_cv;
// True while EmuThread should stop calling RunLoop() and block instead.
std::atomic<bool> s_paused{false};
bool s_auto_muted = false;

// The screen arrangements R3 cycles through.
struct ScreenLayoutPreset {
    Settings::LayoutOption layout;
    bool swap_screen;
    bool upright_screen;
    Settings::SmallScreenPosition small_screen_position;
    const char* name;
};

constexpr std::array<ScreenLayoutPreset, 8> s_layout_presets{{
    {Settings::LayoutOption::Default, false, false, Settings::SmallScreenPosition::BottomRight,
     "Vertical stack"},
    {Settings::LayoutOption::SideScreen, false, false, Settings::SmallScreenPosition::MiddleRight,
     "Side by side"},
    {Settings::LayoutOption::LargeScreen, false, false, Settings::SmallScreenPosition::MiddleRight,
     "Large top, small bottom"},
    {Settings::LayoutOption::LargeScreen, true, false, Settings::SmallScreenPosition::MiddleRight,
     "Large bottom, small top"},
    {Settings::LayoutOption::TopScreenOnly, false, false,
     Settings::SmallScreenPosition::BottomRight, "Top screen only"},
    {Settings::LayoutOption::BottomScreenOnly, false, false,
     Settings::SmallScreenPosition::BottomRight, "Bottom screen only"},
    {Settings::LayoutOption::Default, false, true, Settings::SmallScreenPosition::BottomRight,
     "Vertical stack (rotate console)"},
    {Settings::LayoutOption::HybridScreen, false, false,
     Settings::SmallScreenPosition::BottomRight, "Hybrid screen"},
}};

// Kept consistent with Settings so the first press advances past the boot default.
std::size_t s_layout_index = 0;

// Bitmask of presets R3 cycles through. Bit i set means preset i is in the rotation.
std::atomic<std::uint32_t> s_layout_cycle_mask{(1u << s_layout_presets.size()) - 1};

// Applies the preset at s_layout_index to the live settings and relays out the framebuffer.
void ApplyCurrentLayout() {
    const ScreenLayoutPreset& preset = s_layout_presets[s_layout_index];
    Settings::values.layout_option = preset.layout;
    Settings::values.swap_screen = preset.swap_screen;
    Settings::values.upright_screen = preset.upright_screen;
    Settings::values.small_screen_position = preset.small_screen_position;
    Core::System::GetInstance().GPU().Renderer().UpdateCurrentFramebufferLayout();
    LOG_INFO(Frontend, "Screen layout: {}", preset.name);
}

/// Returns true if `path` is a 3DS title.
bool IsLoadableRom(const std::string& path) {
    try {
        auto loader = Loader::GetLoader(path);
        if (!loader) {
            return false;
        }
        bool executable = false;
        return loader->IsExecutable(executable) == Loader::ResultStatus::Success && executable;
    } catch (const std::exception& e) {
        LOG_WARNING(Frontend, "Exception probing '{}' for loadability: {}", path, e.what());
        return false;
    }
}

/// Picks a ROM to boot
std::string ResolveRomPath(const std::string& rom_arg) {
    if (!rom_arg.empty()) {
        if (IsLoadableRom(rom_arg)) {
            return rom_arg;
        }
        LOG_WARNING(Frontend, "ROM argument '{}' is not a loadable 3DS title", rom_arg);
    }

    const std::string roms_dir = FileUtil::GetUserPath(FileUtil::UserPath::UserDir) + "roms/";
    FileUtil::CreateFullPath(roms_dir);

    std::string found;
    FileUtil::ForeachDirectoryEntry(
        nullptr, roms_dir,
        [&found](u64*, const std::string& directory, const std::string& virtual_name) {
            if (!found.empty()) {
                return true;
            }
            const std::string path = directory + virtual_name;
            if (!FileUtil::IsDirectory(path) && IsLoadableRom(path)) {
                found = path;
            }
            return true;
        });
    return found;
}

void EmuThread(std::string path) {
    if (!Common::Horizon::PinCurrentThread(2)) {
        LOG_WARNING(Frontend, "Failed to pin emulation thread to core 2");
    }

    auto& system = Core::System::GetInstance();
    EmuWindow_Switch* window = GetEmuWindow();
    if (!window) {
        LOG_CRITICAL(Frontend, "No EmuWindow available");
        s_stop = true;
        return;
    }

    // Mesa's Switch driver cannot reliably present renderbuffers across shared contexts
    window->MakeCurrent();

    try {
        const Core::System::ResultStatus load_result = system.Load(*window, path);
        if (load_result != Core::System::ResultStatus::Success) {
            LOG_CRITICAL(Frontend, "Failed to load ROM '{}' (error {})", path,
                         static_cast<int>(load_result));
            window->DoneCurrent();
            s_stop = true;
            return;
        }

        s_load_ok = true;

        u64 program_id = 0;
        system.GetAppLoader().ReadProgramId(program_id);
        system.GPU().ApplyPerProgramSettings(program_id);

        // Load any cached disk shaders
        system.GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(
            s_stop,
            [](VideoCore::LoadCallbackStage, std::size_t, std::size_t, const std::string&) {});

        LOG_INFO(Frontend, "Emulation started (program id {:016X})", program_id);
        while (!s_stop) {
            if (s_paused.load(std::memory_order_relaxed)) {
                {
                    std::unique_lock<std::mutex> lock(s_pause_mutex);
                    s_pause_cv.wait_for(lock, std::chrono::milliseconds(16), [] {
                        return !s_paused.load(std::memory_order_relaxed) ||
                               s_stop.load(std::memory_order_relaxed);
                    });
                }
                if (s_paused.load(std::memory_order_relaxed) &&
                    !s_stop.load(std::memory_order_relaxed)) {
                    system.GPU().Renderer().SwapBuffers();
                }
                continue;
            }
            const Core::System::ResultStatus result = system.RunLoop();
            if (result == Core::System::ResultStatus::Success) {
                continue;
            }
            if (result == Core::System::ResultStatus::ShutdownRequested) {
                LOG_INFO(Frontend, "Guest requested shutdown");
            } else {
                LOG_CRITICAL(Frontend, "Emulation halted: {} (error {})", system.GetStatusDetails(),
                             static_cast<int>(result));
            }
            break;
        }
    } catch (const std::exception& e) {
        // A malformed/encrypted ROM can drive the loader into an oversized allocation or
        // similar; without this, that exception would propagate out of the thread entry
        // point and call std::terminate(), hard-crashing the whole app instead of just
        // failing this one launch. s_load_ok is left as-is: if this fired before it was
        // set, LoadFailed() still correctly reports the launch as failed.
        LOG_CRITICAL(Frontend, "Emulation thread threw an exception loading/running '{}': {}", path,
                     e.what());
    }

    window->DoneCurrent();
    s_stop = true;
}

} // namespace

bool BootRom(const std::string& rom_arg) {
    const std::string path = ResolveRomPath(rom_arg);
    if (path.empty()) {
        LOG_CRITICAL(Frontend, "No loadable ROM found. Place a .3ds/.cci/.cxi/.3dsx in {}roms/",
                     FileUtil::GetUserPath(FileUtil::UserPath::UserDir));
        return false;
    }

    LOG_INFO(Frontend, "Booting ROM {}", path);

    s_load_ok = false;
    s_paused = false;
    auto& system = Core::System::GetInstance();
    FileUtil::SetCurrentRomPath(path);

    // Register the loader early so the core reuses it during Load.
    std::unique_ptr<Loader::AppLoader> app_loader;
    try {
        app_loader = Loader::GetLoader(path);
    } catch (const std::exception& e) {
        LOG_WARNING(Frontend, "Exception creating loader for '{}': {}", path, e.what());
    }
    if (app_loader) {
        system.RegisterAppLoaderEarly(app_loader);
    }

    system.ApplySettings();
    Settings::LogSettings();

    // Hand text input to Horizon's swkbd.
    Frontend::RegisterDefaultApplets(system);
    RegisterKeyboard(system);

    // Transfer ownership of the window context from the main thread to the emulation thread.
    auto* window = GetEmuWindow();
    if (!window) {
        LOG_CRITICAL(Frontend, "No EmuWindow available");
        return false;
    }
    window->DoneCurrent();

    s_stop = false;
    s_emu_thread = std::thread(EmuThread, path);
    return true;
}

bool IsRunning() {
    return !s_stop;
}

void PauseEmulation() {
    if (s_stop || s_paused.load(std::memory_order_relaxed)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s_pause_mutex);
        s_paused.store(true, std::memory_order_relaxed);
    }
    if (!Settings::values.audio_muted) {
        Settings::values.audio_muted = true;
        s_auto_muted = true;
    }
}

void ResumeEmulation() {
    if (!s_paused.load(std::memory_order_relaxed)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s_pause_mutex);
        s_paused.store(false, std::memory_order_relaxed);
    }
    s_pause_cv.notify_all();
    if (s_auto_muted) {
        Settings::values.audio_muted = false;
        s_auto_muted = false;
    }
}

bool IsPaused() {
    return s_paused.load(std::memory_order_relaxed);
}

void StepScreenLayout(int delta) {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return;
    }

    const int count = static_cast<int>(s_layout_presets.size());
    s_layout_index = static_cast<std::size_t>(((static_cast<int>(s_layout_index) + delta) % count +
                                               count) %
                                              count);
    ApplyCurrentLayout();
}

void CycleScreenLayout() {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return;
    }

    const std::uint32_t mask = s_layout_cycle_mask.load(std::memory_order_relaxed);
    const int count = static_cast<int>(s_layout_presets.size());
    // Advance to the next preset that is enabled in R3's rotation.
    for (int step = 1; step <= count; ++step) {
        const int idx = (static_cast<int>(s_layout_index) + step) % count;
        if ((mask & (1u << idx)) != 0) {
            s_layout_index = static_cast<std::size_t>(idx);
            ApplyCurrentLayout();
            return;
        }
    }
}

void MirrorScreenSides() {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return;
    }

    using Settings::SmallScreenPosition;
    const auto layout = Settings::values.layout_option.GetValue();
    const bool uses_small_screen_position = layout == Settings::LayoutOption::LargeScreen ||
                                            layout == Settings::LayoutOption::SideScreen;
    const bool is_single_screen = layout == Settings::LayoutOption::TopScreenOnly ||
                                  layout == Settings::LayoutOption::BottomScreenOnly;

    if (is_single_screen) {
        // TopScreenOnly/BottomScreenOnly pick their screen purely from layout_option
        // (see framebuffer_layout.cpp) and ignore swap_screen entirely, so mirroring
        // has to flip the layout itself rather than the swap_screen flag.
        Settings::values.layout_option = layout == Settings::LayoutOption::TopScreenOnly
                                             ? Settings::LayoutOption::BottomScreenOnly
                                             : Settings::LayoutOption::TopScreenOnly;
        LOG_INFO(Frontend, "Swapped single screen");
    } else if (uses_small_screen_position) {
        SmallScreenPosition pos = Settings::values.small_screen_position.GetValue();
        switch (pos) {
        case SmallScreenPosition::TopRight:
            pos = SmallScreenPosition::TopLeft;
            break;
        case SmallScreenPosition::MiddleRight:
            pos = SmallScreenPosition::MiddleLeft;
            break;
        case SmallScreenPosition::BottomRight:
            pos = SmallScreenPosition::BottomLeft;
            break;
        case SmallScreenPosition::TopLeft:
            pos = SmallScreenPosition::TopRight;
            break;
        case SmallScreenPosition::MiddleLeft:
            pos = SmallScreenPosition::MiddleRight;
            break;
        case SmallScreenPosition::BottomLeft:
            pos = SmallScreenPosition::BottomRight;
            break;
        case SmallScreenPosition::AboveLarge:
            pos = SmallScreenPosition::BelowLarge;
            break;
        case SmallScreenPosition::BelowLarge:
            pos = SmallScreenPosition::AboveLarge;
            break;
        }
        Settings::values.small_screen_position = pos;
        LOG_INFO(Frontend, "Mirrored small screen position");
    } else {
        Settings::values.swap_screen = !Settings::values.swap_screen.GetValue();
        LOG_INFO(Frontend, "Swapped screen sides");
    }

    system.GPU().Renderer().UpdateCurrentFramebufferLayout();
}

const char* CurrentScreenLayoutName() {
    return s_layout_presets[s_layout_index].name;
}

int GetScreenLayoutCount() {
    return static_cast<int>(s_layout_presets.size());
}

const char* GetScreenLayoutName(int index) {
    if (index < 0 || index >= static_cast<int>(s_layout_presets.size())) {
        return "";
    }
    return s_layout_presets[index].name;
}

std::uint32_t GetLayoutCycleMask() {
    return s_layout_cycle_mask.load(std::memory_order_relaxed);
}

void SetLayoutCycleMask(std::uint32_t mask) {
    const std::uint32_t all = (1u << s_layout_presets.size()) - 1;
    s_layout_cycle_mask.store(mask & all, std::memory_order_relaxed);
}

bool LoadFailed() {
    return !s_load_ok;
}

void StopRom() {
    s_stop = true;
    s_pause_cv.notify_all(); // wake EmuThread if it's parked waiting for resume
    CancelKeyboard();
    if (s_emu_thread.joinable()) {
        s_emu_thread.join();
    }
    // Tear the core down after the window context current.
    auto* window = GetEmuWindow();
    if (window) {
        window->MakeCurrent();
    }
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        system.Shutdown();
    }
    s_paused = false;
    if (s_auto_muted) {
        Settings::values.audio_muted = false;
        s_auto_muted = false;
    }
}

} // namespace SwitchFrontend
