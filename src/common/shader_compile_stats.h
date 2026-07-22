// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

// Tracks how many background shader/pipeline compiles are in flight, across every compile
// source (PICA JIT, Vulkan shader disk cache, Vulkan pipeline cache), so the frontend can show
// the user a "Compiling shaders: x/y" indicator explaining an otherwise-mysterious slowdown.
//
// Deliberately avoids common_types.h here (see shader_compile_stats.cpp): its u128 typedef
// conflicts with <switch.h>'s on Switch, the same reason horizon_thread.h avoids it too.
namespace Common::ShaderCompileStats {

// Call right before queuing a background compile job.
void BeginCompile();

// Call once that job finishes, regardless of whether it succeeded.
void EndCompile();

struct Progress {
    std::uint32_t done;
    std::uint32_t total;
};

// The current batch's progress, or nullopt if there's nothing in flight right now.
std::optional<Progress> GetProgress();

// Raises the CPU clock (and drops the GPU clock) while at least one caller holds the boost.
// Reference-counted, so it nests safely with concurrent shader compiles and other boost holders
// (ROM load, CIA install). No-op off Switch.
void AcquireCpuBoost();
void ReleaseCpuBoost();

// RAII wrapper around Acquire/ReleaseCpuBoost for scope-bound holders.
class ScopedCpuBoost {
public:
    ScopedCpuBoost() {
        AcquireCpuBoost();
    }
    ~ScopedCpuBoost() {
        ReleaseCpuBoost();
    }
    ScopedCpuBoost(const ScopedCpuBoost&) = delete;
    ScopedCpuBoost& operator=(const ScopedCpuBoost&) = delete;
};

} // namespace Common::ShaderCompileStats
