// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/horizon_thread.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Common::Horizon {

#ifdef __SWITCH__

namespace {
bool TryPin(std::uint32_t core_id) {
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
} // namespace

bool PinCurrentThread(std::uint32_t core_id) {
    return TryPin(core_id);
}

bool PinCurrentThreadPreferred(std::initializer_list<std::uint32_t> preferred) {
    for (const std::uint32_t core_id : preferred) {
        if (TryPin(core_id)) {
            return true;
        }
    }
    return false;
}

std::uint64_t GetTotalMemorySize() {
    u64 size{};
    if (R_FAILED(svcGetInfo(&size, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) {
        return 0;
    }
    return size;
}

#else

bool PinCurrentThread(std::uint32_t) {
    return false;
}

bool PinCurrentThreadPreferred(std::initializer_list<std::uint32_t>) {
    return false;
}

std::uint64_t GetTotalMemorySize() {
    return 0;
}

#endif

} // namespace Common::Horizon
