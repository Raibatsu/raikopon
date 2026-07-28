// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

// Touch- and controller-driven editor for Settings::LayoutOption::CustomLayout, plus each screen's
// opacity (Settings::values.top_screen_opacity/bottom_screen_opacity) — neither is exposed as a
// Settings-tab row; both only exist as a property of "the layout" you set up here. The custom
// rects are absolute pixels in the 1280x720 output (see EmuWindow::UpdateCurrentFramebufferLayout,
// which hands CustomFrameLayout the real window size), and the Switch touchscreen reports in that
// same space, so touch coordinates are used unscaled. Docked mode has no touchscreen at all, so
// L/R selects a screen, the left stick moves it, the right stick stretches its width/height
// independently, and ZL/ZR uniformly scale it with the aspect ratio always locked — the touch
// drag/pinch/X-lock-toggle path is completely unchanged by any of this. D-pad Left/Right (either
// mode) adjusts the selected screen's opacity, which has no touch gesture of its own.
namespace SwitchFrontend {

struct InputState;

// One frame of editor buttons, edge-triggered by the caller (mirrors QuickMenuNav) except
// grow/shrink/opacity_up/opacity_down, which are level-triggered (held) so controller adjustment
// feels like holding a button rather than repeatedly tapping one.
struct LayoutEditorNav {
    bool confirm{};       // A - keep the arrangement and close
    bool cancel{};        // B - discard and close
    bool toggle_lock{};   // X - lock/unlock the native aspect ratio while pinching (touch only)
    bool reset{};         // Minus - back to the built-in arrangement
    bool cycle_select{};  // L or R - switch which screen the sticks/ZL/ZR/D-pad act on
    bool grow{};          // ZR held - scale the selected screen up, aspect always locked
    bool shrink{};        // ZL held - scale the selected screen down, aspect always locked
    bool opacity_up{};    // D-pad Right held - raise the selected screen's opacity
    bool opacity_down{};  // D-pad Left held - lower the selected screen's opacity
};

// Switches to the Custom preset, seeding its rects from the current arrangement the first time,
// and pauses emulation. No-op if no game is running.
void OpenLayoutEditor();

// `save` keeps the edited rects and records them as a per-game override; otherwise the rects from
// the moment the editor opened are put back. Either way emulation resumes.
void CloseLayoutEditor(bool save);

bool IsLayoutEditorOpen();

// Drives the editor from the input thread.
void UpdateLayoutEditor(const InputState& state, const LayoutEditorNav& nav);

// Puts both screens back to the built-in Custom arrangement.
void ResetLayoutEditor();

} // namespace SwitchFrontend
