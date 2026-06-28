// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <string>
#include <thread>

#include "citra_switch/config.h"
#include "citra_switch/emu_window.h"
#include "common/file_util.h"
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

/// Returns true if `path` is a 3DS title.
bool IsLoadableRom(const std::string& path) {
    auto loader = Loader::GetLoader(path);
    if (!loader) {
        return false;
    }
    bool executable = false;
    return loader->IsExecutable(executable) == Loader::ResultStatus::Success && executable;
}

/// Picks a ROM to boot
std::string ResolveRomPath(const std::string& rom_arg) {
    if (!rom_arg.empty()) {
        if (IsLoadableRom(rom_arg)) {
            return rom_arg;
        }
        LOG_WARNING(Frontend, "ROM argument '{}' is not a loadable 3DS title",
                    rom_arg);
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
    auto& system = Core::System::GetInstance();
    EmuWindow_Switch* window = GetEmuWindow();
    if (!window) {
        LOG_CRITICAL(Frontend, "No EmuWindow available");
        s_stop = true;
        return;
    }

    // The renderer is created on this thread through the window's shared context.
    window->MakeCurrent();

    const Core::System::ResultStatus load_result = system.Load(*window, path);
    if (load_result != Core::System::ResultStatus::Success) {
        LOG_CRITICAL(Frontend, "Failed to load ROM '{}' (error {})", path,
                     static_cast<int>(load_result));
        window->DoneCurrent();
        s_stop = true;
        return;
    }

    u64 program_id = 0;
    system.GetAppLoader().ReadProgramId(program_id);
    system.GPU().ApplyPerProgramSettings(program_id);

    // Load any cached disk shaders
    system.GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(
        s_stop,
        [](VideoCore::LoadCallbackStage, std::size_t, std::size_t, const std::string&) {});

    LOG_INFO(Frontend, "Emulation started (program id {:016X})", program_id);
    while (!s_stop) {
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

    auto& system = Core::System::GetInstance();
    FileUtil::SetCurrentRomPath(path);

    // Register the loader early so the core reuses it during Load.
    auto app_loader = Loader::GetLoader(path);
    if (app_loader) {
        system.RegisterAppLoaderEarly(app_loader);
    }

    system.ApplySettings();
    Settings::LogSettings();

    // TODO: Actually support applets via switch keyboard and such
    Frontend::RegisterDefaultApplets(system);

    s_stop = false;
    s_emu_thread = std::thread(EmuThread, path);
    return true;
}

bool IsRunning() {
    return !s_stop;
}

void StopRom() {
    s_stop = true;
    if (s_emu_thread.joinable()) {
        s_emu_thread.join();
    }
    // Tear the core down after the emulation thread has released its context.
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        system.Shutdown();
    }
}

} // namespace SwitchFrontend
