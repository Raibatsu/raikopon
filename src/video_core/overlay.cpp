// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <mutex>

#include "video_core/overlay.h"

namespace VideoCore {

namespace {
std::mutex s_editor_mutex;
LayoutEditorState s_editor_state;
std::atomic<bool> s_editor_visible{false};
} // namespace

void SetLayoutEditorState(const LayoutEditorState& state) {
    {
        std::scoped_lock lock{s_editor_mutex};
        s_editor_state = state;
    }
    s_editor_visible.store(state.visible, std::memory_order_release);
}

LayoutEditorState GetLayoutEditorState() {
    std::scoped_lock lock{s_editor_mutex};
    return s_editor_state;
}

bool IsLayoutEditorVisible() {
    return s_editor_visible.load(std::memory_order_acquire);
}

} // namespace VideoCore
