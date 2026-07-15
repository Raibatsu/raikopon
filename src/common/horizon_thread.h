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

} // namespace Common::Horizon
