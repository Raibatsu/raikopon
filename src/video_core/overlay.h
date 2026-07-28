// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

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

} // namespace VideoCore
