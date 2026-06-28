// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <deko3d.h>

#include "common/common_types.h"

namespace Deko3D {

/// Rounds value up to the next multiple of alignment.
[[nodiscard]] constexpr u32 AlignUp(u32 value, u32 alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace Deko3D
