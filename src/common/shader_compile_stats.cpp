// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/shader_compile_stats.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Common::ShaderCompileStats {

namespace {
std::mutex mutex;
std::uint32_t done = 0;
std::uint32_t total = 0;

#ifdef __SWITCH__
// CPU-boosts for the duration of a compile batch. ApmCpuBoostMode_FastLoad also throttles
// the GPU to its minimum clock, which is free on a true loading screen (GPU idle) but a real
// gamble during active gameplay, where the GPU is busy rendering at the same time. Trying it
// here anyway per request; watch for rendering getting worse during a burst, not just faster
// compiles, since that would mean the GPU throttle is costing more than the CPU boost buys.
bool boosted = false;

void SetBoost(bool active) {
    if (active == boosted) {
        return;
    }
    appletSetCpuBoostMode(active ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
    boosted = active;
}
#endif
} // namespace

void BeginCompile() {
    std::lock_guard lock{mutex};
    if (done >= total) {
        // The previous batch is fully drained (or this is the first ever compile);
        // start counting a fresh one instead of accumulating a lifetime total.
        done = 0;
        total = 0;
    }
    ++total;
#ifdef __SWITCH__
    SetBoost(true);
#endif
}

void EndCompile() {
    std::lock_guard lock{mutex};
    if (done < total) {
        ++done;
    }
#ifdef __SWITCH__
    if (done >= total) {
        SetBoost(false);
    }
#endif
}

std::optional<Progress> GetProgress() {
    std::lock_guard lock{mutex};
    if (total == 0 || done >= total) {
        return std::nullopt;
    }
    return Progress{done, total};
}

} // namespace Common::ShaderCompileStats
