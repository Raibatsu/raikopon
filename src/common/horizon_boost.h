// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Common::Horizon {

// Raises the CPU clock (and drops the GPU clock) for the duration of the scope.
class CpuBoostScope {
public:
    CpuBoostScope();
    ~CpuBoostScope();

    CpuBoostScope(const CpuBoostScope&) = delete;
    CpuBoostScope& operator=(const CpuBoostScope&) = delete;
};

} // namespace Common::Horizon
