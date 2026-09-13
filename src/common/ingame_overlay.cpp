// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/ingame_overlay.h"

#include <atomic>
#include <mutex>

namespace Common::IngameOverlay {

namespace {

std::mutex s_mutex;
State s_state;
std::atomic<bool> s_open{false};
std::atomic<float> s_hold_progress{0.0f};

} // namespace

void SetState(State state) {
    s_open.store(state.open, std::memory_order_relaxed);
    std::scoped_lock lock{s_mutex};
    s_state = std::move(state);
}

State GetState() {
    std::scoped_lock lock{s_mutex};
    return s_state;
}

bool IsOpen() {
    return s_open.load(std::memory_order_relaxed);
}

void SetHoldProgress(float progress) {
    s_hold_progress.store(progress, std::memory_order_relaxed);
}

float GetHoldProgress() {
    return s_hold_progress.load(std::memory_order_relaxed);
}

} // namespace Common::IngameOverlay
