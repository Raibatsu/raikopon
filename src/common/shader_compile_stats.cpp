// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/shader_compile_stats.h"

#ifdef __SWITCH__
#include <atomic>

#include <switch.h>
#endif

namespace Common::ShaderCompileStats {

namespace {
std::mutex mutex;
std::uint32_t done = 0;
std::uint32_t total = 0;

#ifdef __SWITCH__
// Depth-counted so ROM load / CIA install can hold the boost across a stretch that itself
// triggers shader compiles, without an inner EndCompile() dropping the clock early. See
// ApmCpuBoostMode_FastLoad note on AcquireCpuBoost below.
std::atomic<int> boost_depth{0};
#endif
} // namespace

void AcquireCpuBoost() {
#ifdef __SWITCH__
    // ApmCpuBoostMode_FastLoad also throttles the GPU to its minimum clock, which is free on a
    // true loading screen (GPU idle) but a real gamble during active gameplay, where the GPU is
    // busy rendering at the same time. Watch for rendering getting worse during a burst, not
    // just faster compiles, since that would mean the GPU throttle is costing more than the CPU
    // boost buys.
    if (boost_depth.fetch_add(1, std::memory_order_relaxed) == 0) {
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    }
#endif
}

void ReleaseCpuBoost() {
#ifdef __SWITCH__
    if (boost_depth.fetch_sub(1, std::memory_order_relaxed) == 1) {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    }
#endif
}

void BeginCompile() {
    std::lock_guard lock{mutex};
    if (done >= total) {
        // The previous batch is fully drained (or this is the first ever compile);
        // start counting a fresh one instead of accumulating a lifetime total.
        done = 0;
        total = 0;
    }
    ++total;
    AcquireCpuBoost();
}

void EndCompile() {
    std::lock_guard lock{mutex};
    if (done < total) {
        ++done;
    }
    ReleaseCpuBoost();
}

std::optional<Progress> GetProgress() {
    std::lock_guard lock{mutex};
    if (total == 0 || done >= total) {
        return std::nullopt;
    }
    return Progress{done, total};
}

} // namespace Common::ShaderCompileStats
