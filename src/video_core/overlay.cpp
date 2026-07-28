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

std::mutex s_canvas_mutex;
MenuCanvas s_canvas;
std::atomic<bool> s_canvas_visible{false};
std::atomic<std::uint64_t> s_canvas_version{0};
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

void SetMenuCanvas(const std::uint32_t* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        ClearMenuCanvas();
        return;
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::uint64_t version = s_canvas_version.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::scoped_lock lock{s_canvas_mutex};
        s_canvas.pixels.assign(pixels, pixels + count);
        s_canvas.width = width;
        s_canvas.height = height;
        s_canvas.version = version;
        s_canvas.visible = true;
    }
    s_canvas_visible.store(true, std::memory_order_release);
}

void ClearMenuCanvas() {
    {
        std::scoped_lock lock{s_canvas_mutex};
        s_canvas.visible = false;
        s_canvas.pixels.clear();
        s_canvas.width = 0;
        s_canvas.height = 0;
    }
    s_canvas_visible.store(false, std::memory_order_release);
}

MenuCanvas GetMenuCanvas() {
    std::scoped_lock lock{s_canvas_mutex};
    return s_canvas;
}

bool IsMenuCanvasVisible() {
    return s_canvas_visible.load(std::memory_order_acquire);
}

std::uint64_t GetMenuCanvasVersion() {
    return s_canvas_version.load(std::memory_order_acquire);
}

} // namespace VideoCore
