// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/layout_editor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "citra_switch/config.h"
#include "citra_switch/game_settings.h"
#include "citra_switch/input.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/gpu.h"
#include "video_core/overlay.h"
#include "video_core/renderer_base.h"

namespace SwitchFrontend {

namespace {

// The output the custom rects are expressed in. Matches kSwitchScreenWidth/Height in
// emu_window.cpp, which is what CustomFrameLayout is handed.
constexpr int kCanvasWidth = 1280;
constexpr int kCanvasHeight = 720;

// Small enough to tuck a screen into a corner, large enough to still be grabbable.
constexpr int kMinScreenSize = 120;

// 3DS panel proportions: 400x240 top, 320x240 bottom.
constexpr float kTopAspect = 400.0f / 240.0f;
constexpr float kBottomAspect = 320.0f / 240.0f;

// Controller-driven move/resize, for docked mode (no touchscreen at all). Rates are per real
// second, not per call — UpdateLayoutEditor is driven by RunGame's frame loop, which has no fixed
// rate of its own (it can poll far faster than 60Hz, e.g. while the emulator is paused and not
// vsync-gated), so a flat per-call delta would run at whatever speed that loop happens to spin at
// instead of a speed the player actually chose. Move/stretch speed additionally scale with how far
// each stick is pushed past the deadzone; ZL/ZR scale and D-pad opacity are flat rates while held.
constexpr std::int32_t kStickDeadzone = 8000;
constexpr float kMoveSpeedPxPerSecond = 250.0f;    // Left stick.
constexpr float kStretchSpeedPxPerSecond = 200.0f; // Right stick, independent width/height.
constexpr float kScaleSpeedPxPerSecond = 150.0f;   // ZL/ZR, aspect always locked.
constexpr float kOpacityPercentPerSecond = 40.0f;  // D-pad Left/Right.

// Caps the per-call elapsed time fed into the speeds above, so a stall (e.g. the first call after
// opening, or a hitch elsewhere in the frame loop) can't produce one huge jump.
constexpr float kMaxDeltaSeconds = 0.1f;

struct Rect {
    int x{}, y{}, w{}, h{};
};

enum class Grab { None, Top, Bottom };

bool s_active = false;
bool s_aspect_locked = true;
Grab s_grabbed = Grab::None;

// Offset from the grabbed screen's origin to the finger, so dragging doesn't snap the corner
// to the touch point.
int s_drag_offset_x = 0;
int s_drag_offset_y = 0;
bool s_was_touching = false;

// Pinch baselines, captured when the second finger lands.
bool s_pinching = false;
float s_pinch_span_x = 0.0f;
float s_pinch_span_y = 0.0f;
float s_pinch_span = 0.0f;
Rect s_pinch_start{};

// Restored by CloseLayoutEditor(false).
Rect s_entry_top{};
Rect s_entry_bottom{};
Settings::LayoutOption s_entry_layout{};

// Wall-clock time of the last UpdateLayoutEditor call, so move/resize/opacity speeds can be
// expressed per second rather than per call (see kMaxDeltaSeconds's comment above).
std::chrono::steady_clock::time_point s_last_update{};

// Seconds since the last call, reset by OpenLayoutEditor so the first frame after opening never
// sees a stale/huge gap.
float ConsumeDeltaSeconds() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - s_last_update).count();
    s_last_update = now;
    return std::clamp(dt, 0.0f, kMaxDeltaSeconds);
}

// Fractional remainders for the controller-driven controls below, so a delta smaller than one
// pixel/percent (e.g. a tiny dt from a fast poll rate) doesn't just round away to nothing every
// single call — the leftover carries forward until it accumulates to a whole unit.
float s_move_accum_x = 0.0f;
float s_move_accum_y = 0.0f;
float s_stretch_accum_w = 0.0f;
float s_stretch_accum_h = 0.0f;
float s_scale_accum_h = 0.0f;
float s_opacity_accum = 0.0f;

int StepAccumulator(float& accum, float delta) {
    accum += delta;
    const auto whole = static_cast<int>(accum);
    accum -= static_cast<float>(whole);
    return whole;
}

Rect GetTopRect() {
    const auto& v = Settings::values;
    return {v.custom_top_x.GetValue(), v.custom_top_y.GetValue(), v.custom_top_width.GetValue(),
            v.custom_top_height.GetValue()};
}

Rect GetBottomRect() {
    const auto& v = Settings::values;
    return {v.custom_bottom_x.GetValue(), v.custom_bottom_y.GetValue(),
            v.custom_bottom_width.GetValue(), v.custom_bottom_height.GetValue()};
}

void SetTopRect(const Rect& r) {
    auto& v = Settings::values;
    v.custom_top_x = static_cast<u16>(r.x);
    v.custom_top_y = static_cast<u16>(r.y);
    v.custom_top_width = static_cast<u16>(r.w);
    v.custom_top_height = static_cast<u16>(r.h);
}

void SetBottomRect(const Rect& r) {
    auto& v = Settings::values;
    v.custom_bottom_x = static_cast<u16>(r.x);
    v.custom_bottom_y = static_cast<u16>(r.y);
    v.custom_bottom_width = static_cast<u16>(r.w);
    v.custom_bottom_height = static_cast<u16>(r.h);
}

int GetOpacity(Grab which) {
    return which == Grab::Top ? Settings::values.top_screen_opacity.GetValue()
                               : Settings::values.bottom_screen_opacity.GetValue();
}

void SetOpacity(Grab which, int percent) {
    const auto clamped = static_cast<u16>(std::clamp(percent, 0, 100));
    if (which == Grab::Top) {
        Settings::values.top_screen_opacity = clamped;
    } else {
        Settings::values.bottom_screen_opacity = clamped;
    }
}

// Deadzone-adjusted axis value in [-1, 1], scaled linearly past the deadzone.
float Axis(std::int32_t v) {
    if (std::abs(v) <= kStickDeadzone) {
        return 0.0f;
    }
    const float sign = v < 0 ? -1.0f : 1.0f;
    return sign * std::clamp(
                      (std::abs(v) - kStickDeadzone) / static_cast<float>(32767 - kStickDeadzone),
                      0.0f, 1.0f);
}

// Keeps a rect on-canvas and above the minimum size. Size is clamped before position so a screen
// that was resized past the edge gets pulled back in rather than silently cropped.
Rect Clamp(Rect r) {
    r.w = std::clamp(r.w, kMinScreenSize, kCanvasWidth);
    r.h = std::clamp(r.h, kMinScreenSize, kCanvasHeight);
    r.x = std::clamp(r.x, 0, kCanvasWidth - r.w);
    r.y = std::clamp(r.y, 0, kCanvasHeight - r.h);
    return r;
}

bool Contains(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void Relayout() {
    auto& system = Core::System::GetInstance();
    if (system.IsPoweredOn()) {
        system.GPU().Renderer().UpdateCurrentFramebufferLayout();
    }
}

// Mirrors the built-in "Vertical stack" proportions at 1.5x, which fills 1280x720 exactly.
void ApplyDefaults() {
    SetTopRect({(kCanvasWidth - 600) / 2, 0, 600, 360});
    SetBottomRect({(kCanvasWidth - 480) / 2, 360, 480, 360});
    SetOpacity(Grab::Top, 100);
    SetOpacity(Grab::Bottom, 100);
}

// Publishes the rects so the renderer can outline them and show which one is grabbed.
void Publish() {
    VideoCore::LayoutEditorState state;
    state.visible = s_active;
    if (s_active) {
        const Rect top = GetTopRect();
        const Rect bottom = GetBottomRect();
        state.top = {top.x, top.y, top.w, top.h};
        state.bottom = {bottom.x, bottom.y, bottom.w, bottom.h};
        state.canvas_width = kCanvasWidth;
        state.canvas_height = kCanvasHeight;
        state.selected_top = s_grabbed == Grab::Top;
        state.selected_bottom = s_grabbed == Grab::Bottom;
        state.aspect_locked = s_aspect_locked;
        // The hint bar itself (button chips + short labels, not a single long sentence) is built
        // directly in RendererVulkan::PrepareLayoutEditor from aspect_locked above — see that
        // function for why plain text got too wide to fit the screen.
    }
    VideoCore::SetLayoutEditorState(state);
}

// Docked mode has no touchscreen, so this is the controller equivalent of the touch drag/pinch
// path above, but split into two distinct resize inputs instead of one toggle-locked one: the
// right stick always stretches width/height independently (the "can distort" control, no aspect
// lock — X's lock toggle only affects the touch pinch), and ZL/ZR always scale uniformly about
// the centre with the aspect ratio locked (the "can't distort" control), so a controller player
// gets both without needing to fiddle with the lock toggle at all. No-op if nothing is selected
// yet (see cycle_select in UpdateLayoutEditor, which is what first picks Top or Bottom for a
// controller-only player).
void UpdateLayoutEditorFromController(const InputState& state, const LayoutEditorNav& nav,
                                      float dt) {
    if (s_grabbed == Grab::None) {
        return;
    }

    const Rect current = s_grabbed == Grab::Top ? GetTopRect() : GetBottomRect();
    Rect next = current;
    bool changed = false;

    // ZL/ZR: uniform scale about the centre, aspect ratio always locked.
    if (nav.grow || nav.shrink) {
        const float dir = nav.grow ? 1.0f : -1.0f;
        const int dh = StepAccumulator(s_scale_accum_h, dir * kScaleSpeedPxPerSecond * dt);
        if (dh != 0) {
            const float aspect = s_grabbed == Grab::Top ? kTopAspect : kBottomAspect;
            const float cx = current.x + current.w * 0.5f;
            const float cy = current.y + current.h * 0.5f;
            const float new_h = current.h + dh;
            const float new_w = new_h * aspect;
            next.w = static_cast<int>(std::lround(new_w));
            next.h = static_cast<int>(std::lround(new_h));
            next.x = static_cast<int>(std::lround(cx - next.w * 0.5f));
            next.y = static_cast<int>(std::lround(cy - next.h * 0.5f));
            changed = true;
        }
    }

    // Right stick: width/height stretched independently, never aspect-locked.
    const int dw = StepAccumulator(s_stretch_accum_w, Axis(state.right_x) *
                                                          kStretchSpeedPxPerSecond * dt);
    const int dh2 = StepAccumulator(s_stretch_accum_h, Axis(state.right_y) *
                                                           kStretchSpeedPxPerSecond * dt);
    if (dw != 0 || dh2 != 0) {
        next.w += dw;
        next.h += dh2;
        changed = true;
    }

    // Left stick: move. Matches this codebase's stick convention elsewhere (e.g.
    // ingame_settings.cpp): positive y is up, positive x is right.
    const int dx = StepAccumulator(s_move_accum_x, Axis(state.left_x) * kMoveSpeedPxPerSecond * dt);
    const int dy = StepAccumulator(s_move_accum_y, Axis(state.left_y) * kMoveSpeedPxPerSecond * dt);
    if (dx != 0 || dy != 0) {
        next.x += dx;
        next.y -= dy;
        changed = true;
    }

    if (!changed) {
        return;
    }
    const Rect clamped = Clamp(next);
    if (s_grabbed == Grab::Top) {
        SetTopRect(clamped);
    } else {
        SetBottomRect(clamped);
    }
    Relayout();
    Publish();
}

} // namespace

bool IsLayoutEditorOpen() {
    return s_active;
}

void OpenLayoutEditor() {
    if (s_active) {
        return;
    }
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return;
    }

    auto& v = Settings::values;
    s_entry_layout = v.layout_option.GetValue();
    s_entry_top = GetTopRect();
    s_entry_bottom = GetBottomRect();

    // A fresh profile has the desktop defaults (bottom at y=500, past the 720 canvas), which would
    // open the editor with the bottom screen jammed against the edge. Treat anything that doesn't
    // fit as unset and start from the built-in arrangement instead.
    const Rect top = Clamp(s_entry_top);
    const Rect bottom = Clamp(s_entry_bottom);
    const bool fits = top.x == s_entry_top.x && top.y == s_entry_top.y &&
                      top.w == s_entry_top.w && top.h == s_entry_top.h &&
                      bottom.x == s_entry_bottom.x && bottom.y == s_entry_bottom.y &&
                      bottom.w == s_entry_bottom.w && bottom.h == s_entry_bottom.h;
    if (!fits) {
        ApplyDefaults();
    }

    BeginFieldOverride(OverrideField::ScreenLayout);
    v.layout_option = Settings::LayoutOption::CustomLayout;

    s_active = true;
    s_grabbed = Grab::None;
    s_pinching = false;
    s_was_touching = false;
    s_last_update = std::chrono::steady_clock::now();
    PauseEmulation();
    Relayout();
    Publish();
    LOG_INFO(Frontend, "Layout editor opened");
}

void CloseLayoutEditor(bool save) {
    if (!s_active) {
        return;
    }
    auto& v = Settings::values;
    if (save) {
        // layout_option is already CustomLayout; snapshotting here records both it and the rects
        // against this title.
        MarkGameOverride(OverrideField::ScreenLayout);
        FlushGameOverrides();
        SyncScreenLayoutIndex();
    } else {
        SetTopRect(s_entry_top);
        SetBottomRect(s_entry_bottom);
        v.layout_option = s_entry_layout;
    }

    s_active = false;
    s_grabbed = Grab::None;
    s_pinching = false;
    s_was_touching = false;
    Publish();
    Relayout();
    ResumeEmulation();
    LOG_INFO(Frontend, "Layout editor closed ({})", save ? "saved" : "discarded");
}

void ResetLayoutEditor() {
    ApplyDefaults();
    s_grabbed = Grab::None;
    s_pinching = false;
    Relayout();
    Publish();
}

void UpdateLayoutEditor(const InputState& state, const LayoutEditorNav& nav) {
    if (!s_active) {
        return;
    }
    const float dt = ConsumeDeltaSeconds();

    if (nav.cancel) {
        CloseLayoutEditor(false);
        return;
    }
    if (nav.confirm) {
        CloseLayoutEditor(true);
        return;
    }
    if (nav.reset) {
        ResetLayoutEditor();
        return;
    }
    if (nav.toggle_lock) {
        s_aspect_locked = !s_aspect_locked;
        Publish();
    }
    if (nav.cycle_select) {
        // Only two screens exist, so L and R both just toggle which one the stick/ZL/ZR act on;
        // None -> Top on the first press so a controller-only (docked) player isn't stuck with
        // nothing selected before ever touching the screen.
        s_grabbed = s_grabbed == Grab::Top ? Grab::Bottom : Grab::Top;
        s_pinching = false;
        Publish();
    }
    // Opacity has no touch gesture of its own, so D-pad Left/Right work the same regardless of
    // whether the player is also touching a screen right now — unlike move/resize, which fully
    // hand off between touch and controller (see the `!touching` branch below).
    if ((nav.opacity_up || nav.opacity_down) && s_grabbed != Grab::None) {
        const float dir = nav.opacity_up ? 1.0f : -1.0f;
        const int delta = StepAccumulator(s_opacity_accum, dir * kOpacityPercentPerSecond * dt);
        if (delta != 0) {
            SetOpacity(s_grabbed, GetOpacity(s_grabbed) + delta);
            Relayout();
            Publish();
        }
    }

    const std::uint32_t touches = std::min<std::uint32_t>(state.touch_count, 2);
    const bool touching = touches > 0 && state.touches[0].pressed;

    if (!touching) {
        // Only clear the grab when a real touch was just released — in docked mode there's never
        // any touch at all, and s_grabbed is instead driven by cycle_select above, so it needs to
        // persist across frames with no touch rather than resetting every single one.
        if (s_was_touching) {
            s_pinching = false;
            s_was_touching = false;
        }
        UpdateLayoutEditorFromController(state, nav, dt);
        return;
    }

    const int x0 = static_cast<int>(state.touches[0].x);
    const int y0 = static_cast<int>(state.touches[0].y);

    // A new contact picks a screen. Bottom is tested first so it wins where the two overlap,
    // matching the draw order.
    if (!s_was_touching) {
        const Rect bottom = GetBottomRect();
        const Rect top = GetTopRect();
        if (Contains(bottom, x0, y0)) {
            s_grabbed = Grab::Bottom;
            s_drag_offset_x = x0 - bottom.x;
            s_drag_offset_y = y0 - bottom.y;
        } else if (Contains(top, x0, y0)) {
            s_grabbed = Grab::Top;
            s_drag_offset_x = x0 - top.x;
            s_drag_offset_y = y0 - top.y;
        } else {
            s_grabbed = Grab::None;
        }
        s_pinching = false;
    }
    s_was_touching = true;

    if (s_grabbed == Grab::None) {
        Publish();
        return;
    }

    const Rect current = s_grabbed == Grab::Top ? GetTopRect() : GetBottomRect();
    const auto commit = [&](const Rect& r) {
        const Rect clamped = Clamp(r);
        if (s_grabbed == Grab::Top) {
            SetTopRect(clamped);
        } else {
            SetBottomRect(clamped);
        }
        Relayout();
    };

    if (touches >= 2 && state.touches[1].pressed) {
        const int x1 = static_cast<int>(state.touches[1].x);
        const int y1 = static_cast<int>(state.touches[1].y);
        const float span_x = std::fabs(static_cast<float>(x1 - x0));
        const float span_y = std::fabs(static_cast<float>(y1 - y0));
        const float span = std::sqrt(span_x * span_x + span_y * span_y);

        if (!s_pinching) {
            // Baselines are captured once so the resize tracks the whole gesture rather than
            // compounding per-frame deltas (which drift).
            s_pinching = true;
            s_pinch_span = std::max(span, 1.0f);
            s_pinch_span_x = std::max(span_x, 1.0f);
            s_pinch_span_y = std::max(span_y, 1.0f);
            s_pinch_start = current;
        } else {
            const float cx = s_pinch_start.x + s_pinch_start.w * 0.5f;
            const float cy = s_pinch_start.y + s_pinch_start.h * 0.5f;
            float new_w, new_h;
            if (s_aspect_locked) {
                const float scale = std::clamp(span / s_pinch_span, 0.1f, 10.0f);
                const float aspect = s_grabbed == Grab::Top ? kTopAspect : kBottomAspect;
                new_h = s_pinch_start.h * scale;
                new_w = new_h * aspect;
            } else {
                const float scale_x = std::clamp(span_x / s_pinch_span_x, 0.1f, 10.0f);
                const float scale_y = std::clamp(span_y / s_pinch_span_y, 0.1f, 10.0f);
                new_w = s_pinch_start.w * scale_x;
                new_h = s_pinch_start.h * scale_y;
            }
            // Grow/shrink about the centre so the screen stays where the user put it.
            Rect resized;
            resized.w = static_cast<int>(std::lround(new_w));
            resized.h = static_cast<int>(std::lround(new_h));
            resized.x = static_cast<int>(std::lround(cx - resized.w * 0.5f));
            resized.y = static_cast<int>(std::lround(cy - resized.h * 0.5f));
            commit(resized);
        }
        Publish();
        return;
    }

    // Back to one finger: drop the pinch baseline and re-anchor the drag so the screen doesn't
    // jump when the second finger lifts.
    if (s_pinching) {
        s_pinching = false;
        s_drag_offset_x = std::clamp(x0 - current.x, 0, std::max(current.w - 1, 0));
        s_drag_offset_y = std::clamp(y0 - current.y, 0, std::max(current.h - 1, 0));
    }

    Rect moved = current;
    moved.x = x0 - s_drag_offset_x;
    moved.y = y0 - s_drag_offset_y;
    commit(moved);
    Publish();
}

} // namespace SwitchFrontend
