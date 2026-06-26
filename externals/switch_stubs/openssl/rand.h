// SPDX-FileCopyrightText: Azahar Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Shim for Switch compatibility

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// libnx CSRNG
void randomGet(void* buf, size_t len);

static inline int RAND_bytes(unsigned char* buf, int num) {
    randomGet(buf, (size_t)num);
    return 1;
}

#ifdef __cplusplus
}
#endif
