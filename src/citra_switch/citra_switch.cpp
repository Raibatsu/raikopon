// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <string>
#include <switch.h>

#include "citra_switch/config.h"

int main(int argc, char* argv[]) {
    const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
    if (have_socket) {
        nxlinkStdio();
    }
    std::printf("Dekopon: an Azahar port for the Nintendo Switch\n");

    // Resolve SD-card dirs and create folders/files if not present
    const int launch_count = SwitchFrontend::Bootstrap();
    std::printf("FS & logging up (launch #%d). Logs are located at sdmc:/switch/dekopon/log/\n",
                launch_count);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // The EmuWindow owns the EGL/GLES context on the default nwindow.
    if (!SwitchFrontend::CreateWindow(nwindowGetDefault())) {
        std::printf("EmuWindow no worky.\n");
        SwitchFrontend::Shutdown();
        if (have_socket) {
            socketExit();
        }
        return 1;
    }
    std::printf("EmuWindow worky.\n");

    // Either take argv[1] or search sdmc:/switch/dekopon/roms/ for roms.
    const std::string rom_arg = (argc > 1 && argv[1] != nullptr) ? argv[1] : std::string{};

    if (SwitchFrontend::BootRom(rom_arg)) {
        std::printf("Booting ROM...\n");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
                break;
            }
            if (!SwitchFrontend::IsRunning()) {
                std::printf("Emulation stopped.\n");
                break;
            }
            SwitchFrontend::PresentFrame();
        }
        SwitchFrontend::StopRom();
    } else {
        std::printf("No ROM to boot. Put one in sdmc:/switch/dekopon/roms/.\n");
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
                break;
            }
            SwitchFrontend::ClearFrame();
        }
    }

    SwitchFrontend::DestroyWindow();
    SwitchFrontend::Shutdown();
    if (have_socket) {
        socketExit();
    }
    return 0;
}
