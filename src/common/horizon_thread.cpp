// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/horizon_thread.h"
#include "common/logging/log.h"

#ifdef __SWITCH__
#include <bit>

// Hand-declared rather than including <switch.h>: that header's own `u128` typedef
// (__uint128_t) conflicts with common_types.h's (std::array<uint64_t, 2>), and this file needs
// both real libnx syscalls and the logging macros above, unlike the rest of horizon_thread.h's
// callers - the same workaround common/shader_compile_stats.h/.cpp use for the identical reason.
extern "C" {
u32 svcGetInfo(u64* out, u32 id0, u32 handle, u64 id1);
u32 svcSetThreadCoreMask(u32 handle, s32 preferred_core, u32 affinity_mask);
u32 svcGetThreadCoreMask(s32* out_preferred_core, u64* out_affinity_mask, u32 handle);
}
constexpr u32 CurProcessHandle = 0xFFFF8001;
constexpr u32 CurThreadHandle = 0xFFFF8000;
constexpr u32 InfoTypeCoreMask = 0;
constexpr u32 InfoTypeTotalMemorySize = 6;
#endif

namespace Common::Horizon {

#ifdef __SWITCH__

namespace {
u64 AllowedCoreMask() {
    u64 allowed_cores{};
    if (svcGetInfo(&allowed_cores, InfoTypeCoreMask, CurProcessHandle, 0) != 0) {
        return 0;
    }
    return allowed_cores;
}

bool TryPin(std::uint32_t core_id) {
    if (core_id >= 32) {
        return false;
    }

    const u64 allowed_cores = AllowedCoreMask();
    const u32 core_mask = 1U << core_id;
    if ((allowed_cores & core_mask) == 0) {
        LOG_WARNING(Common,
                    "Switch thread affinity: core {} not in process's allowed mask 0x{:x}",
                    core_id, allowed_cores);
        return false;
    }

    return svcSetThreadCoreMask(CurThreadHandle, static_cast<s32>(core_id), core_mask) == 0;
}
} // namespace

bool PinCurrentThread(std::uint32_t core_id) {
    const bool ok = TryPin(core_id);
    LOG_INFO(Common, "Switch thread affinity: pin core={} ok={}", core_id, ok);
    return ok;
}

bool PinCurrentThreadPreferred(std::initializer_list<std::uint32_t> preferred) {
    for (const std::uint32_t core_id : preferred) {
        if (TryPin(core_id)) {
            LOG_INFO(Common, "Switch thread affinity: pin preferred core={} (succeeded)",
                     core_id);
            return true;
        }
    }
    LOG_WARNING(Common, "Switch thread affinity: failed to pin to any of {} preferred cores",
                preferred.size());
    return false;
}

bool PinCurrentThreadAffinity(std::int32_t preferred_core, std::uint64_t affinity_mask) {
    const u64 allowed_cores = AllowedCoreMask();
    const u64 masked_affinity = affinity_mask & allowed_cores;
    if (masked_affinity == 0) {
        LOG_WARNING(Common,
                    "Switch thread affinity: requested mask 0x{:x} has no overlap with allowed "
                    "mask 0x{:x}",
                    affinity_mask, allowed_cores);
        return false;
    }

    s32 actual_preferred = preferred_core;
    if (preferred_core < 0 || preferred_core >= 32 ||
        (allowed_cores & (1ULL << preferred_core)) == 0) {
        actual_preferred = static_cast<s32>(std::countr_zero(masked_affinity));
    }

    const bool ok = svcSetThreadCoreMask(CurThreadHandle, actual_preferred,
                                         static_cast<u32>(masked_affinity)) == 0;
    s32 readback_preferred = -1;
    u64 readback_affinity = 0;
    svcGetThreadCoreMask(&readback_preferred, &readback_affinity, CurThreadHandle);
    LOG_INFO(Common,
             "Switch thread affinity: set preferred={} mask=0x{:x} ok={} -> readback "
             "preferred={} mask=0x{:x}",
             actual_preferred, masked_affinity, ok, readback_preferred, readback_affinity);
    return ok;
}

std::uint64_t GetTotalMemorySize() {
    u64 size{};
    if (svcGetInfo(&size, InfoTypeTotalMemorySize, CurProcessHandle, 0) != 0) {
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

bool PinCurrentThreadAffinity(std::int32_t, std::uint64_t) {
    return false;
}

std::uint64_t GetTotalMemorySize() {
    return 0;
}

#endif

} // namespace Common::Horizon
