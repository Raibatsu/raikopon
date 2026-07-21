// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/horizon_boost.h"

#ifdef __SWITCH__
#include <atomic>

#include <switch.h>
#endif

namespace Common::Horizon {

#ifdef __SWITCH__

namespace {
std::atomic<int> s_boost_depth{0};
} // namespace

CpuBoostScope::CpuBoostScope() {
    if (s_boost_depth.fetch_add(1) == 0) {
        appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    }
}

CpuBoostScope::~CpuBoostScope() {
    if (s_boost_depth.fetch_sub(1) == 1) {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    }
}

#else

CpuBoostScope::CpuBoostScope() = default;
CpuBoostScope::~CpuBoostScope() = default;

#endif

} // namespace Common::Horizon
