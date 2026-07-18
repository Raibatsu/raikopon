// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <mutex>

#include "video_core/overlay.h"

namespace VideoCore {

namespace {
std::mutex s_mutex;
OverlayMenuState s_state;
std::atomic<bool> s_visible{false};
} // namespace

void SetOverlayMenuState(const OverlayMenuState& state) {
    {
        std::scoped_lock lock{s_mutex};
        s_state = state;
    }
    s_visible.store(state.visible, std::memory_order_release);
}

OverlayMenuState GetOverlayMenuState() {
    std::scoped_lock lock{s_mutex};
    return s_state;
}

bool IsOverlayMenuVisible() {
    return s_visible.load(std::memory_order_acquire);
}

} // namespace VideoCore
