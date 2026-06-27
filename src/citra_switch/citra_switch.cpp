// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
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

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }

        SwitchFrontend::PresentFrame();
    }

    SwitchFrontend::DestroyWindow();
    SwitchFrontend::Shutdown();
    if (have_socket) {
        socketExit();
    }
    return 0;
}
