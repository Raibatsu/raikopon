// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// Horizon/newlib system-call shims for the .nro link.

#include <cstddef>
#include <switch.h>

extern "C" int getentropy(void* buffer, size_t length) {
    randomGet(buffer, length);
    return 0;
}
