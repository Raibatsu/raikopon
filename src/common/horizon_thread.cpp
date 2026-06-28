// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/horizon_thread.h"

#include <switch.h>

namespace Common::Horizon {

bool PinCurrentThread(std::uint32_t core_id) {
    if (core_id >= 32) {
        return false;
    }

    u64 allowed_cores{};
    if (R_FAILED(svcGetInfo(&allowed_cores, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) {
        return false;
    }

    const u32 core_mask = 1U << core_id;
    if ((allowed_cores & core_mask) == 0) {
        return false;
    }

    return R_SUCCEEDED(
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, static_cast<s32>(core_id), core_mask));
}

} // namespace Common::Horizon
