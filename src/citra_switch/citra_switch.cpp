// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// Basic display screen that does nothing currently

#include <cstdio>
#include <switch.h>

int main(int argc, char* argv[]) {
    consoleInit(nullptr);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::printf("Dekopon, an Azahar port for the Nintendo Switch\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
