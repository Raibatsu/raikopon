// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A render-agnostic description of an on-screen overlay menu. The frontend fills it in from the
// input thread and the active renderer reads it back on the emulation thread to draw it on top of
// the game.
namespace VideoCore {

// A screen rectangle in the editor's canvas space (the same absolute output pixels the
// custom_top_*/custom_bottom_* settings use).
struct LayoutEditorRect {
    int x{}, y{}, w{}, h{};
};

// Description of the touch layout editor, drawn over the game while it is open.
struct LayoutEditorState {
    bool visible{};
    LayoutEditorRect top;
    LayoutEditorRect bottom;
    int canvas_width{};
    int canvas_height{};
    bool selected_top{};
    bool selected_bottom{};
    bool aspect_locked{};
};

void SetLayoutEditorState(const LayoutEditorState& state);
LayoutEditorState GetLayoutEditorState();
bool IsLayoutEditorVisible();

// The settings screen rendered as a CPU RGBA canvas (0xAABBGGRR words, i.e. R,G,B,A bytes) and
// handed to the active renderer to draw over the game. `version` bumps on every published frame so
// the renderer only re-uploads when the pixels actually changed.
struct MenuCanvas {
    bool visible{};
    std::vector<std::uint32_t> pixels;
    int width{};
    int height{};
    std::uint64_t version{};
};

// Publishes a newly drawn canvas (called from the input thread). Bumps the version.
void SetMenuCanvas(const std::uint32_t* pixels, int width, int height);

// Hides the canvas without publishing pixels.
void ClearMenuCanvas();

// Copies the current canvas out (called from the emulation/render thread).
MenuCanvas GetMenuCanvas();

// Cheap check so the renderer can skip the copy above entirely.
bool IsMenuCanvasVisible();

// The version of the latest published canvas, so the renderer can skip re-uploading.
std::uint64_t GetMenuCanvasVersion();

} // namespace VideoCore
