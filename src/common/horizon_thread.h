// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <initializer_list>

namespace Common::Horizon {

// Pins the calling thread to `core_id`.
bool PinCurrentThread(std::uint32_t core_id);

// Pins the calling thread to the first core in `preferred` that the process is
// allowed to run on.
bool PinCurrentThreadPreferred(std::initializer_list<std::uint32_t> preferred);

// Sets `preferred_core` as the scheduler hint but lets the thread run on any core set in
// `affinity_mask` (bit i = core i), so it can float between them instead of being pinned
// exclusively to one. Bits for cores the process isn't allowed to use are dropped first; if
// `preferred_core` itself isn't allowed, the lowest allowed core in the (masked) affinity is
// used instead.
bool PinCurrentThreadAffinity(std::int32_t preferred_core, std::uint64_t affinity_mask);

// Returns the total memory pool available to the process in bytes.
std::uint64_t GetTotalMemorySize();

} // namespace Common::Horizon
