// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <switch.h>

#include "citra_switch/applets/swkbd.h"
#include "citra_switch/config.h"
#include "citra_switch/game_settings.h"
#include "citra_switch/glyph_atlas.h"
#include "citra_switch/gpu_canvas.h"
#include "citra_switch/ingame_cheats.h"
#include "citra_switch/input.h"
#include "citra_switch/layout_editor.h"
#include "citra_switch/library_cheats.h"
#include "citra_switch/settings_model.h"
#include "citra_switch/ui_badge.h"
#include "citra_switch/ui_cheats.h"
#include "citra_switch/ui_credits.h"
#include "citra_switch/ui_gamedetails.h"
#include "citra_switch/ui_game_icons.h"
#include "citra_switch/ui_gamesettings.h"
#include "citra_switch/ui_hintbar.h"
#include "citra_switch/ui_icon_atlas.h"
#include "citra_switch/ui_input_bindings.h"
#include "citra_switch/ui_install.h"
#include "citra_switch/ui_layout.h"
#include "citra_switch/ui_library.h"
#include "citra_switch/ui_model.h"
#include "citra_switch/ui_mods.h"
#include "citra_switch/ui_page_transition.h"
#include "citra_switch/ui_paths.h"
#include "citra_switch/ui_settings.h"
#include "citra_switch/ui_splash.h"
#include "citra_switch/ui_strings.h"
#include "citra_switch/ui_theme.h"
#include "citra_switch/ui_updates.h"
#include "citra_switch/updater.h"
#include "citra_switch/titledb.h"
#include "common/horizon_thread.h"
#include "common/ingame_overlay.h"
#include "common/loading_icon.h"

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;
}

namespace {

constexpr std::array<std::pair<u64, SwitchFrontend::InputButton>, 16> button_map{{
    {HidNpadButton_A, SwitchFrontend::InputButton::A},
    {HidNpadButton_B, SwitchFrontend::InputButton::B},
    {HidNpadButton_X, SwitchFrontend::InputButton::X},
    {HidNpadButton_Y, SwitchFrontend::InputButton::Y},
    {HidNpadButton_Up, SwitchFrontend::InputButton::Up},
    {HidNpadButton_Down, SwitchFrontend::InputButton::Down},
    {HidNpadButton_Left, SwitchFrontend::InputButton::Left},
    {HidNpadButton_Right, SwitchFrontend::InputButton::Right},
    {HidNpadButton_L, SwitchFrontend::InputButton::L},
    {HidNpadButton_R, SwitchFrontend::InputButton::R},
    {HidNpadButton_Plus, SwitchFrontend::InputButton::Start},
    {HidNpadButton_Minus, SwitchFrontend::InputButton::Select},
    {HidNpadButton_ZL, SwitchFrontend::InputButton::ZL},
    {HidNpadButton_ZR, SwitchFrontend::InputButton::ZR},
    {HidNpadButton_StickL, SwitchFrontend::InputButton::L3},
    {HidNpadButton_StickR, SwitchFrontend::InputButton::R3},
}};

std::array<HidSixAxisSensorHandle, 4> six_axis_handles{};

void StartSixAxis() {
    hidGetSixAxisSensorHandles(&six_axis_handles[0], 1, HidNpadIdType_Handheld,
                               HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&six_axis_handles[1], 1, HidNpadIdType_No1,
                               HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&six_axis_handles[2], 2, HidNpadIdType_No1,
                               HidNpadStyleTag_NpadJoyDual);
    for (const HidSixAxisSensorHandle& handle : six_axis_handles) {
        hidStartSixAxisSensor(handle);
    }
}

void StopSixAxis() {
    for (const HidSixAxisSensorHandle& handle : six_axis_handles) {
        hidStopSixAxisSensor(handle);
    }
}

// Returns false when libnx's HID shared memory is unavailable, in which case padUpdate() must
// not be called (it hard-aborts the whole process via diagAbortWithResult instead of failing
// gracefully). Also logs the transition so a real occurrence can be confirmed after the fact.
bool HidShmemReady(const char* site) {
    static const u64 start_tick = armGetSystemTick();
    static void* prev_addr = hidGetSharedmemAddr();
    void* addr = hidGetSharedmemAddr();
    if (addr != prev_addr) {
        if (FILE* f = std::fopen("sdmc:/switch/azahar/hid_shmem_debug.log", "a")) {
            const double seconds =
                static_cast<double>(armTicksToNs(armGetSystemTick() - start_tick)) / 1'000'000'000.0;
            std::fprintf(f, "[%9.3f] site=%s hid_shmem %p -> %p\n", seconds, site, prev_addr, addr);
            std::fflush(f);
            std::fclose(f);
            fsdevCommitDevice("sdmc");
        }
        prev_addr = addr;
    }
    return addr != nullptr;
}

SwitchFrontend::MotionState PollMotion(PadState& pad) {
    if (!HidShmemReady("PollMotion")) {
        return {};
    }
    const u64 style_set = padGetStyleSet(&pad);
    HidSixAxisSensorState sensor{};
    bool read = false;

    if ((style_set & HidNpadStyleTag_NpadHandheld) != 0) {
        read = hidGetSixAxisSensorStates(six_axis_handles[0], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadFullKey) != 0) {
        read = hidGetSixAxisSensorStates(six_axis_handles[1], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadJoyDual) != 0) {
        const u64 attributes = padGetAttributes(&pad);
        if ((attributes & HidNpadAttribute_IsLeftConnected) != 0) {
            read = hidGetSixAxisSensorStates(six_axis_handles[2], &sensor, 1) > 0;
        } else if ((attributes & HidNpadAttribute_IsRightConnected) != 0) {
            read = hidGetSixAxisSensorStates(six_axis_handles[3], &sensor, 1) > 0;
        }
    }

    if (!read) {
        return {};
    }
    return {
        .active = true,
        .accel_x = sensor.acceleration.x,
        .accel_y = sensor.acceleration.y,
        .accel_z = sensor.acceleration.z,
        .gyro_x = sensor.angular_velocity.x,
        .gyro_y = sensor.angular_velocity.y,
        .gyro_z = sensor.angular_velocity.z,
    };
}

u64 PollInput(PadState& pad, SwitchFrontend::InputState& state) {
    if (HidShmemReady("PollInput")) {
        padUpdate(&pad);
    }
    const u64 held = padGetButtons(&pad);
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);

    state = SwitchFrontend::InputState{
        .left_x = left.x,
        .left_y = left.y,
        .right_x = right.x,
        .right_y = right.y,
        .motion = PollMotion(pad),
    };
    for (const auto& [source, target] : button_map) {
        if ((held & source) != 0) {
            state.buttons |= SwitchFrontend::ButtonMask(target);
        }
    }

    HidTouchScreenState touch{};
    if (HidShmemReady("PollInput.touch") && hidGetTouchScreenStates(&touch, 1) != 0 &&
        touch.count > 0) {
        state.touch_pressed = true;
        state.touch_x = touch.touches[0].x;
        state.touch_y = touch.touches[0].y;
        state.touch_count = std::min<std::uint32_t>(touch.count, state.touches.size());
        for (std::uint32_t i = 0; i < state.touch_count; ++i) {
            state.touches[i] = SwitchFrontend::TouchPoint{true, touch.touches[i].x,
                                                          touch.touches[i].y};
        }
    }
    return held;
}

struct DirectionRepeat {
    float held_time = 0.0f;
    float next_fire = 0.0f;
    bool was_held = false;
};

bool StepRepeat(DirectionRepeat& state, bool held, bool pressed_edge, float dt) {
    constexpr float kRepeatDelay = 0.4f;
    constexpr float kRepeatInterval = 0.1f;
    if (!held) {
        state.was_held = false;
        return false;
    }
    if (pressed_edge || !state.was_held) {
        state.was_held = true;
        state.held_time = 0.0f;
        state.next_fire = kRepeatDelay;
        return true;
    }
    state.held_time += dt;
    if (state.held_time >= state.next_fire) {
        state.next_fire += kRepeatInterval;
        return true;
    }
    return false;
}

void EnterLibrary(SwitchFrontend::UiModel& model) {
    model.app_state = SwitchFrontend::AppState::Library;
    model.library_panel = SwitchFrontend::LibraryPanel::List;
}

std::string RunLauncher(PadState& pad, SwitchFrontend::UiModel& model) {
    SwitchFrontend::GpuCanvas canvas;
    if (!canvas.Init()) {
        std::printf("gpu_canvas Init() failed.\n");
        model.exit_requested = true;
        return {};
    }

    SwitchFrontend::GlyphAtlas atlas;
    if (!atlas.Init(canvas, 24.0f)) {
        std::printf("glyph_atlas Init() failed.\n");
    }
    // Loads the Switch OS's own shared font(s) for whatever language System Language is
    // currently set to, so CJK/Korean/Cyrillic UI text has somewhere to come from beyond the
    // bundled Latin-only font - see glyph_atlas.cpp's SetLanguage(). ui_settings.cpp calls this
    // again if the user changes System Language mid-session.
    atlas.SetLanguage(SwitchFrontend::GetMenuSettings().language);
    SwitchFrontend::ResetCoverCache();
    SwitchFrontend::LoadUiIcons(canvas, atlas);
    SwitchFrontend::LoadBadgeAssets(canvas, atlas);
    // `canvas`/`atlas` are fresh objects every call - Library's already-scanned game list
    // (model.games, which persists across calls) still needs its icons re-uploaded into this new
    // atlas, since GameIconRegion just holds UV coordinates that meant nothing without them.
    model.game_icons_loaded = false;

    {
        constexpr std::time_t kAutoCheckCooldownSeconds = 24 * 60 * 60;
        const std::time_t now = std::time(nullptr);
        const std::time_t last_check = SwitchFrontend::GetLastAutoUpdateCheckTime();
        if (SwitchFrontend::GetAutoCheckUpdatesEnabled() &&
            (last_check <= 0 || now - last_check >= kAutoCheckCooldownSeconds)) {
            SwitchFrontend::StartUpdateCheck(SwitchFrontend::GetUpdateChannel());
            SwitchFrontend::SetLastAutoUpdateCheckTime(now);
        }
    }

    if (model.app_state != SwitchFrontend::AppState::Splash) {
        EnterLibrary(model);
    }

    u64 prev_held = 0;
    u64 last_tick = armGetSystemTick();
    DirectionRepeat repeat_up;
    DirectionRepeat repeat_down;
    DirectionRepeat repeat_left;
    DirectionRepeat repeat_right;
    bool prev_touched = false;
    float touch_start_x = 0.0f;
    float touch_start_y = 0.0f;
    float touch_last_x = 0.0f;
    float touch_last_y = 0.0f;
    float touch_prev_frame_y = 0.0f;

    while (appletMainLoop()) {
        SwitchFrontend::PumpKeyboard();

        if (HidShmemReady("mainloop")) {
            padUpdate(&pad);
        }
        const u64 held = padGetButtons(&pad);
        const u64 held_before_this_frame = prev_held;
        const u64 pressed = held & ~prev_held;
        prev_held = held;

        const u64 now_tick = armGetSystemTick();
        const float dt =
            static_cast<float>(armTicksToNs(now_tick - last_tick)) / 1'000'000'000.0f;
        last_tick = now_tick;

        // A single shared animation clock, ticked once here regardless of which screen is active,
        // so animations (rail tilt wobble, percentage-bar wave fill, Controls capture pulse) stay
        // phase-continuous across screen switches instead of each screen keeping its own out-of-
        // sync copy. Individual consumers apply their own frequency on top of this raw seconds
        // value. Wrapped well before float precision on sin() would start to matter.
        model.wave_elapsed = std::fmod(model.wave_elapsed + dt, 10000.0f);

        HidTouchScreenState touch_state{};
        const bool touched = HidShmemReady("mainloop.touch") &&
                             hidGetTouchScreenStates(&touch_state, 1) != 0 &&
                             touch_state.count > 0;
        bool touch_tap = false;
        float touch_x = 0.0f;
        float touch_y = 0.0f;
        bool touch_held = false;
        bool touch_press_edge = false;
        float touch_held_x = 0.0f;
        float touch_held_y = 0.0f;
        float touch_delta_y = 0.0f;
        const float touch_scale_x = static_cast<float>(canvas.Width()) / 1280.0f;
        const float touch_scale_y = static_cast<float>(canvas.Height()) / 720.0f;
        if (touched) {
            touch_last_x = static_cast<float>(touch_state.touches[0].x);
            touch_last_y = static_cast<float>(touch_state.touches[0].y);
            touch_held = true;
            touch_held_x = touch_last_x * touch_scale_x;
            touch_held_y = touch_last_y * touch_scale_y;
            if (!prev_touched) {
                touch_start_x = touch_last_x;
                touch_start_y = touch_last_y;
                touch_press_edge = true;
            } else {
                touch_delta_y = (touch_last_y - touch_prev_frame_y) * touch_scale_y;
            }
            touch_prev_frame_y = touch_last_y;
        } else if (prev_touched) {
            constexpr float kTapMoveThreshold = 24.0f;
            const float dx = touch_last_x - touch_start_x;
            const float dy = touch_last_y - touch_start_y;
            if ((dx * dx + dy * dy) <= kTapMoveThreshold * kTapMoveThreshold) {
                touch_tap = true;
                touch_x = touch_last_x * touch_scale_x;
                touch_y = touch_last_y * touch_scale_y;
            }
        }
        prev_touched = touched;

        const u64 up_mask = SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Up);
        const u64 down_mask = SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Down);
        const u64 left_mask = SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Left);
        const u64 right_mask =
            SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Right);

        SwitchFrontend::InputButton raw_pressed_button = SwitchFrontend::InputButton::None;
        for (const auto& [source, target] : button_map) {
            if ((pressed & source) != 0) {
                raw_pressed_button = target;
                break;
            }
        }

        // Required chord for every non-directional UI action - every bound bit must be held
        // together, firing on the edge where the full mask newly becomes held. A single-button
        // binding still behaves like a plain press, since a one-bit mask makes this equivalent to
        // (pressed & mask) != 0.
        const auto chord_edge = [&](SwitchFrontend::MenuAction action) {
            const u64 mask = SwitchFrontend::GetMenuActionButtons(action);
            return mask != 0 && (held & mask) == mask && (held_before_this_frame & mask) != mask;
        };

        const SwitchFrontend::MenuInput input{
            .up = StepRepeat(repeat_up, (held & up_mask) != 0, (pressed & up_mask) != 0, dt),
            .down = StepRepeat(repeat_down, (held & down_mask) != 0, (pressed & down_mask) != 0, dt),
            .left = StepRepeat(repeat_left, (held & left_mask) != 0, (pressed & left_mask) != 0, dt),
            .right =
                StepRepeat(repeat_right, (held & right_mask) != 0, (pressed & right_mask) != 0, dt),
            .confirm = chord_edge(SwitchFrontend::MenuAction::Confirm),
            .cancel = chord_edge(SwitchFrontend::MenuAction::Cancel),
            .tab_prev = chord_edge(SwitchFrontend::MenuAction::TabPrev),
            .tab_next = chord_edge(SwitchFrontend::MenuAction::TabNext),
            .minus = chord_edge(SwitchFrontend::MenuAction::Minus),
            .plus = chord_edge(SwitchFrontend::MenuAction::Plus),
            .rail_prev = chord_edge(SwitchFrontend::MenuAction::RailPrev),
            .rail_next = chord_edge(SwitchFrontend::MenuAction::RailNext),
            .reset_default = chord_edge(SwitchFrontend::MenuAction::ResetToDefault),
            .touch_tap = touch_tap,
            .touch_x = touch_x,
            .touch_y = touch_y,
            .touch_held = touch_held,
            .touch_press_edge = touch_press_edge,
            .touch_held_x = touch_held_x,
            .touch_held_y = touch_held_y,
            .touch_delta_y = touch_delta_y,
            .raw_held = held,
            .raw_pressed = pressed,
            .raw_pressed_button = raw_pressed_button,
        };

        SwitchFrontend::UpdateCheckOutcome update_outcome{};
        if (SwitchFrontend::ConsumeFreshUpdateCheckResult(update_outcome) &&
            update_outcome.result == SwitchFrontend::UpdateCheckResult::UpdateAvailable &&
            model.notice_text.empty()) {
            model.notice_text = SwitchFrontend::Tr("citra_switch.update_available_prefix") +
                                update_outcome.info.tag_name +
                                SwitchFrontend::Tr("citra_switch.update_available_suffix");
            model.notice_is_error = false;
        }

        // BeginFrame() below immediately opens an active render pass on the frame's command
        // buffer (it's still recording, not yet submitted, all the way until EndFrame()). A new
        // cover's atlas upload (GetCoverRegion, first time Game Details shows a given title) does
        // its own separate command buffer submit *and a blocking, queue-wide wait* - doing that
        // while another command buffer has an open render pass is exactly the kind of pattern a
        // mobile GPU driver can choke on without ever raising a catchable CPU-side exception,
        // which fits what was actually happening here (a hang with no crash report at all, not a
        // clean abort). Pre-warming the cache here, before the render pass opens, means
        // GetCoverRegion inside UpdateGameDetails below just returns the already-uploaded region
        // instead of touching Vulkan again.
        if (model.library_panel == SwitchFrontend::LibraryPanel::Details &&
            model.selected_game >= 0 &&
            model.selected_game < static_cast<int>(model.games.size())) {
            SwitchFrontend::GetCoverRegion(
                canvas, atlas, model.games[static_cast<std::size_t>(model.selected_game)],
                model.settings.language);
        }

        if (canvas.BeginFrame(SwitchFrontend::CurrentPalette().bg)) {
            model.hint_chip_count = 0;
            if (model.update_ready_to_close) {
                SwitchFrontend::DrawUpdateReadyModal(canvas, atlas);
                model.hint_chips[0] = {
                    SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Confirm),
                    SwitchFrontend::Tr("hint.close")};
                model.hint_chip_count = 1;
            } else if (model.app_state == SwitchFrontend::AppState::Splash) {
                SwitchFrontend::UpdateSplash(model, input, canvas, atlas, dt);
            } else if (model.page_transition_active) {
                SwitchFrontend::UpdatePageTransition(model, canvas, atlas, dt);
            } else if (model.install_open) {
                SwitchFrontend::UpdateInstall(model, input, canvas, atlas);
            } else if (model.game_settings_open) {
                SwitchFrontend::UpdateGameSettings(model, input, canvas, atlas, dt);
            } else if (model.cheats_open) {
                SwitchFrontend::UpdateCheatsScreen(model, input, canvas, atlas, dt);
            } else if (model.mods_open) {
                SwitchFrontend::UpdateModsScreen(model, input, canvas, atlas, dt);
            } else if (model.layout_editor_open) {
                SwitchFrontend::InputState layout_raw_input{};
                for (const auto& [source, target] : button_map) {
                    if ((held & source) != 0) {
                        layout_raw_input.buttons |= SwitchFrontend::ButtonMask(target);
                    }
                }
                const HidAnalogStickState layout_left = padGetStickPos(&pad, 0);
                const HidAnalogStickState layout_right = padGetStickPos(&pad, 1);
                layout_raw_input.left_x = layout_left.x;
                layout_raw_input.left_y = layout_left.y;
                layout_raw_input.right_x = layout_right.x;
                layout_raw_input.right_y = layout_right.y;
                layout_raw_input.touch_pressed = touched;
                if (touched) {
                    layout_raw_input.touch_x = touch_state.touches[0].x;
                    layout_raw_input.touch_y = touch_state.touches[0].y;
                    layout_raw_input.touch_count = std::min<std::uint32_t>(
                        touch_state.count, static_cast<std::uint32_t>(layout_raw_input.touches.size()));
                    for (std::uint32_t i = 0; i < layout_raw_input.touch_count; ++i) {
                        layout_raw_input.touches[i] = SwitchFrontend::TouchPoint{
                            true, touch_state.touches[i].x, touch_state.touches[i].y};
                    }
                }
                const SwitchFrontend::LayoutEditorNav layout_nav{
                    .confirm = (pressed & HidNpadButton_A) != 0,
                    .cancel = (pressed & HidNpadButton_B) != 0,
                    .toggle_lock = (pressed & HidNpadButton_X) != 0,
                    .reset = (pressed & HidNpadButton_Minus) != 0,
                    .cycle_select = (pressed & (HidNpadButton_L | HidNpadButton_R)) != 0,
                    .grow = (held & HidNpadButton_ZR) != 0,
                    .shrink = (held & HidNpadButton_ZL) != 0,
                    .opacity_up = (held & HidNpadButton_Right) != 0,
                    .opacity_down = (held & HidNpadButton_Left) != 0,
                    .toggle_rotation = (pressed & HidNpadButton_Y) != 0,
                    .rotate_cw = (pressed & HidNpadButton_Right) != 0,
                    .rotate_ccw = (pressed & HidNpadButton_Left) != 0,
                    .layer_front = (pressed & HidNpadButton_Up) != 0,
                    .layer_back = (pressed & HidNpadButton_Down) != 0,
                };
                SwitchFrontend::UpdateLayoutPreview(model, layout_raw_input, layout_nav, canvas, atlas);
            } else if (model.library_panel == SwitchFrontend::LibraryPanel::Details) {
                SwitchFrontend::UpdateGameDetails(model, input, canvas, atlas);
            } else if (model.active_rail == SwitchFrontend::RailItem::Settings) {
                SwitchFrontend::UpdateSettings(model, input, canvas, atlas, dt);
            } else if (model.active_rail == SwitchFrontend::RailItem::Credits) {
                SwitchFrontend::UpdateCredits(model, input, canvas, atlas, dt);
            } else {
                SwitchFrontend::UpdateLibrary(model, input, canvas, atlas, dt);
            }
            SwitchFrontend::DrawHintBar(canvas, atlas, model.hint_chips.data(),
                                       model.hint_chip_count);
            canvas.EndFrame();
        }

        if (model.update_ready_to_close && input.confirm) {
            SwitchFrontend::UnmountRomfsForSelfReplace();
            if (SwitchFrontend::FinishInstall()) {
                SwitchFrontend::RelaunchInto(std::string{});
            }
            SwitchFrontend::RemountRomfsAfterFailedSelfReplace();
            model.update_ready_to_close = false;
            model.notice_text = SwitchFrontend::Tr("updates.install.failed");
            model.notice_is_error = true;
        }

        if (!model.pending_install_path.empty()) {
            const std::string install_path = std::move(model.pending_install_path);
            model.pending_install_path.clear();
            SwitchFrontend::RunInstall(model, canvas, atlas, install_path);
        }

        if (model.pending_update_install) {
            model.pending_update_install = false;
            SwitchFrontend::RunUpdateInstall(model, canvas, atlas,
                                             SwitchFrontend::LastUpdateCheckResult().info);
        }

        if (model.cheats_pending_edit_index != -2) {
            const int edit_index = model.cheats_pending_edit_index;
            model.cheats_pending_edit_index = -2;
            const int result_index = SwitchFrontend::EditLibraryCheatFlow(edit_index);
            if (result_index >= 0) {
                model.cheats_row = 1 + result_index;
            }
        }

        if (model.app_state == SwitchFrontend::AppState::Booting || model.exit_requested) {
            break;
        }
        svcSleepThread(4'000'000);
    }

    atlas.Shutdown();
    canvas.Shutdown();

    if (model.app_state != SwitchFrontend::AppState::Booting || !appletMainLoop()) {
        model.exit_requested = true;
        return {};
    }
    return model.pending_rom;
}

bool EnterInGame(const std::string& rom, SwitchFrontend::UiModel& model) {
    model.app_state = SwitchFrontend::AppState::InGame;

    // Hands this title's already-decoded icon to the Vulkan renderer (via a global, since it
    // doesn't otherwise exist yet - it's only constructed inside BootRom() below) for the
    // "Compiling shaders"/"Building pipelines" boot screen's background. Cleared, not left stale,
    // when there's no match (e.g. booted straight from a CLI path the library never scanned).
    // GameTDB cover art was tried here instead of the small SMDH icon and reverted - even
    // downscaled to 160px it corrupted the in-game overlay's text rendering and crashed during
    // pipeline building. Root cause not found (ruled out: LoadingIcon::Set() not copying - it
    // does; a shared descriptor pool with the overlay font - it has its own). Confirmed via a
    // direct A/B: disabling GameTDB covers (falling back to this exact icon path) made both
    // symptoms disappear. Needs real Vulkan validation layers on hardware to debug further -
    // don't retry this blind again.
    const auto game_it = std::find_if(
        model.games.begin(), model.games.end(),
        [&](const SwitchFrontend::GameEntry& entry) { return entry.path == rom; });
    if (game_it != model.games.end() && game_it->icon_size > 0 &&
        game_it->icon.size() ==
            static_cast<std::size_t>(game_it->icon_size) * static_cast<std::size_t>(game_it->icon_size)) {
        Common::LoadingIcon::Set(game_it->icon.data(), static_cast<std::uint32_t>(game_it->icon_size));
    } else {
        Common::LoadingIcon::Clear();
    }

    // Only meaningful when this rom matched a library entry (so its program_id is known) - a
    // title booted by some other path (e.g. a raw CLI arg never scanned into the library) just
    // skips the redirect and reads Cheats/Mods from their current top-level folder, same as
    // before this existed.
    if (game_it != model.games.end()) {
        SwitchFrontend::ApplyRecursivePathsForTitle(game_it->program_id);
    }

    if (!SwitchFrontend::CreateWindow(nwindowGetDefault())) {
        std::printf("EmuWindow no worky.\n");
        model.notice_text = SwitchFrontend::Tr("citra_switch.error.window_failed");
        model.notice_is_error = true;
        SwitchFrontend::RestorePathsAfterGame();
        EnterLibrary(model);
        return false;
    }

    SwitchFrontend::ResetPointer();

    if (!SwitchFrontend::BootRom(rom)) {
        model.notice_text = SwitchFrontend::Tr("citra_switch.error.rom_unsupported");
        model.notice_is_error = true;
        SwitchFrontend::DestroyWindow();
        SwitchFrontend::RestorePathsAfterGame();
        EnterLibrary(model);
        return false;
    }
    return true;
}

bool ExitInGameToLibrary(const std::string& rom, u64 session_start_tick,
                         SwitchFrontend::UiModel& model) {
    model.app_state = SwitchFrontend::AppState::Exiting;

    SwitchFrontend::StopRom();
    const u64 session_ticks = armGetSystemTick() - session_start_tick;
    const std::uint64_t session_seconds = armTicksToNs(session_ticks) / 1'000'000'000ULL;
    SwitchFrontend::AddPlaytime(model.pending_program_id, session_seconds);

    if (SwitchFrontend::ArticDisconnected()) {
        model.notice_text = SwitchFrontend::Tr("citra_switch.error.artic_disconnected");
        model.notice_is_error = false;
    } else if (SwitchFrontend::LoadFailed()) {
        model.notice_text = SwitchFrontend::Tr("citra_switch.error.rom_unsupported");
        model.notice_is_error = true;
    } else if (rom.starts_with("articinio://") || rom.starts_with("articinin://")) {
        const auto installed = SwitchFrontend::GetSystemFileSetupState();
        const bool complete =
            rom.starts_with("articinio://") ? installed.old3ds : installed.new3ds;
        model.notice_text = complete ? SwitchFrontend::Tr("citra_switch.notice.system_setup_complete")
                                     : SwitchFrontend::Tr("citra_switch.notice.system_setup_incomplete");
        model.notice_is_error = !complete;
    }

    SwitchFrontend::CloseLayoutEditor(false);
    SwitchFrontend::DestroyWindow();
    SwitchFrontend::RestorePathsAfterGame();

    EnterLibrary(model);
    return false;
}

// One row per Common::IngameOverlay entry: index 0 is the synthetic "Exit Game" action, index 1
// is "Reset to Default" (this title's overrides only), index 2 opens the touch/controller layout
// editor, index 3 opens the cheat sub-list (see BuildCheatOverlayState), index 4+ mirror
// settings_rows 1:1.
constexpr int kOverlayActionRowCount = 4;

Common::IngameOverlay::State BuildIngameOverlayState(
    const std::vector<SwitchFrontend::SettingRow>& settings_rows, int selected, bool armed) {
    Common::IngameOverlay::State state;
    state.open = true;
    state.title = SwitchFrontend::Tr("overlay.title");
    state.selected = selected;
    state.armed = armed;
    state.rows.push_back({SwitchFrontend::Tr("overlay.exit_game"), "", false});
    state.rows.push_back({SwitchFrontend::Tr("overlay.reset_to_default"), "", false});
    state.rows.push_back({SwitchFrontend::Tr("gamedetails.action.layout"), "", false});
    state.rows.push_back({SwitchFrontend::Tr("gamedetails.action.cheats"), "", false});
    for (const SwitchFrontend::SettingRow& row : settings_rows) {
        state.rows.push_back({row.label, row.value, row.is_header});
    }
    if (selected >= kOverlayActionRowCount &&
        static_cast<std::size_t>(selected - kOverlayActionRowCount) < settings_rows.size()) {
        state.description =
            settings_rows[static_cast<std::size_t>(selected - kOverlayActionRowCount)].description;
    }
    if (armed) {
        state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Left) +
                               std::string("/") +
                               SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Right),
                               SwitchFrontend::Tr("hint.change")});
        state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Confirm),
                               SwitchFrontend::Tr("hint.done")});
    } else {
        state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Confirm),
                               SwitchFrontend::Tr("hint.select")});
        state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Cancel),
                               SwitchFrontend::Tr("hint.close")});
    }
    return state;
}

// Row 0 is always "Add New Cheat"; rows 1+ mirror SwitchFrontend::CheatCount() (the live,
// currently-running title's cheats - see ingame_cheats.h) 1:1.
Common::IngameOverlay::State BuildCheatOverlayState(int selected) {
    Common::IngameOverlay::State state;
    state.open = true;
    state.title = SwitchFrontend::Tr("gamedetails.action.cheats");
    state.selected = selected;
    state.armed = false;
    state.rows.push_back({SwitchFrontend::Tr("cheats.add"), "", false});
    const int count = SwitchFrontend::CheatCount();
    for (int i = 0; i < count; ++i) {
        state.rows.push_back({SwitchFrontend::CheatName(i),
                              SwitchFrontend::CheatEnabled(i) ? SwitchFrontend::Tr("common.on")
                                                              : SwitchFrontend::Tr("common.off"),
                              false});
    }
    state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Confirm),
                           selected == 0 ? SwitchFrontend::Tr("cheats.add")
                                        : SwitchFrontend::Tr("cheats.toggle")});
    if (selected > 0) {
        state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Minus),
                               SwitchFrontend::Tr("cheats.edit")});
        state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Plus),
                               SwitchFrontend::Tr("cheats.delete")});
    }
    state.hints.push_back({SwitchFrontend::PrimaryButtonLabel(SwitchFrontend::MenuAction::Cancel),
                           SwitchFrontend::Tr("hint.back")});
    return state;
}

bool RunGame(PadState& pad, const std::string& rom, SwitchFrontend::UiModel& model) {
    if (!EnterInGame(rom, model)) {
        return false;
    }

    const u64 session_start_tick = armGetSystemTick();
    {
        u64 prev_held = 0;
        u64 exit_chord_start_tick = 0;

        bool overlay_open = false;
        int overlay_row = 0;
        bool overlay_armed = false;
        SwitchFrontend::MenuSettings overlay_settings{};
        SwitchFrontend::MenuSettings overlay_settings_at_arm{};
        bool overlay_exit_requested = false;
        bool cheat_list_open = false;
        int cheat_row = 0;
        DirectionRepeat repeat_ov_left;
        DirectionRepeat repeat_ov_right;
        u64 last_tick = armGetSystemTick();

        while (appletMainLoop()) {
            SwitchFrontend::PumpKeyboard();

            SwitchFrontend::InputState state;
            const u64 held = PollInput(pad, state);
            const u64 pressed = held & ~prev_held;
            const u64 now_tick = armGetSystemTick();
            const float dt = static_cast<float>(armTicksToNs(now_tick - last_tick)) / 1'000'000'000.0f;
            last_tick = now_tick;

            const bool editor_open = !overlay_open && SwitchFrontend::IsLayoutEditorOpen();
            if (editor_open) {
                const SwitchFrontend::LayoutEditorNav editor_nav{
                    .confirm = (pressed & HidNpadButton_A) != 0,
                    .cancel = (pressed & HidNpadButton_B) != 0,
                    .toggle_lock = (pressed & HidNpadButton_X) != 0,
                    .reset = (pressed & HidNpadButton_Minus) != 0,
                    .cycle_select = (pressed & (HidNpadButton_L | HidNpadButton_R)) != 0,
                    .grow = (held & HidNpadButton_ZR) != 0,
                    .shrink = (held & HidNpadButton_ZL) != 0,
                    .opacity_up = (held & HidNpadButton_Right) != 0,
                    .opacity_down = (held & HidNpadButton_Left) != 0,
                    .toggle_rotation = (pressed & HidNpadButton_Y) != 0,
                    .rotate_cw = (pressed & HidNpadButton_Right) != 0,
                    .rotate_ccw = (pressed & HidNpadButton_Left) != 0,
                    .layer_front = (pressed & HidNpadButton_Up) != 0,
                    .layer_back = (pressed & HidNpadButton_Down) != 0,
                };
                SwitchFrontend::UpdateLayoutEditor(state, editor_nav);
                SwitchFrontend::UpdateInput(SwitchFrontend::InputState{});
            }

            const u64 chord =
                SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::ExitToLibrary);
            const bool chord_held =
                !editor_open && !overlay_open && chord != 0 && (held & chord) == chord;
            if (chord_held) {
                SwitchFrontend::UpdateInput(SwitchFrontend::InputState{});
                if (exit_chord_start_tick == 0) {
                    exit_chord_start_tick = armGetSystemTick();
                }
                constexpr double kOverlayHoldSeconds = 1.0;
                const u64 held_ticks = armGetSystemTick() - exit_chord_start_tick;
                const double held_seconds =
                    static_cast<double>(armTicksToNs(held_ticks)) / 1'000'000'000.0;
                Common::IngameOverlay::SetHoldProgress(
                    static_cast<float>(std::clamp(held_seconds / kOverlayHoldSeconds, 0.0, 1.0)));
                if (held_seconds >= kOverlayHoldSeconds) {
                    overlay_open = true;
                    overlay_row = 0;
                    overlay_armed = false;
                    overlay_settings = SwitchFrontend::GetMenuSettings();
                    exit_chord_start_tick = 0;
                    Common::IngameOverlay::SetHoldProgress(0.0f);
                    // PauseEmulation() alone (no window/swapchain release) - the same mechanism
                    // OpenLayoutEditor() already uses successfully. SwapBuffers() keeps re-
                    // presenting the last frame while paused, so the game visibly freezes behind
                    // the overlay without touching the Vulkan suspend/resume path that crashed.
                    SwitchFrontend::PauseEmulation();
                }
                prev_held = held;
                svcSleepThread(1'000'000);
                continue;
            }
            if (exit_chord_start_tick != 0) {
                Common::IngameOverlay::SetHoldProgress(0.0f);
            }
            exit_chord_start_tick = 0;

            if (overlay_open) {
                SwitchFrontend::UpdateInput(SwitchFrontend::InputState{});

                const std::vector<SwitchFrontend::SettingRow> settings_rows =
                    SwitchFrontend::BuildPerGameSettingRows(overlay_settings);
                const int total_rows = kOverlayActionRowCount + static_cast<int>(settings_rows.size());
                // The 3 fixed action rows (Exit/Reset/Layout) never have headers; only indices at
                // or past kOverlayActionRowCount can, via settings_rows[idx - kOverlayActionRowCount].
                const auto is_header_row = [&](int idx) {
                    return idx >= kOverlayActionRowCount &&
                          settings_rows[static_cast<std::size_t>(idx - kOverlayActionRowCount)]
                              .is_header;
                };
                const auto move_overlay_row = [&](int from, int dir) {
                    int idx = from;
                    for (;;) {
                        const int next = idx + dir;
                        if (next < 0 || next >= total_rows) {
                            return idx;
                        }
                        idx = next;
                        if (!is_header_row(idx)) {
                            return idx;
                        }
                    }
                };

                const u64 up_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Up);
                const u64 down_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Down);
                const u64 left_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Left);
                const u64 right_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Right);
                const u64 confirm_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Confirm);
                const u64 cancel_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Cancel);
                const u64 minus_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Minus);
                const u64 plus_mask =
                    SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::Plus);
                const bool ov_up = (pressed & up_mask) != 0;
                const bool ov_down = (pressed & down_mask) != 0;
                // Auto-repeats while held, same as the launcher menu loop's left/right (StepRepeat)
                // - holding the stick to rapidly cycle a percentage value (CPU Clock, Movie
                // Throttle Clock, ...) shouldn't require mashing the button instead.
                const bool ov_left =
                    StepRepeat(repeat_ov_left, (held & left_mask) != 0, (pressed & left_mask) != 0, dt);
                const bool ov_right = StepRepeat(repeat_ov_right, (held & right_mask) != 0,
                                                 (pressed & right_mask) != 0, dt);
                // Required chord, same as the launcher menu loop's chord_edge - every bound bit
                // must be held together, not just any one of them.
                const auto ov_chord_edge = [&](u64 mask) {
                    return mask != 0 && (held & mask) == mask && (prev_held & mask) != mask;
                };
                const bool ov_confirm = ov_chord_edge(confirm_mask);
                const bool ov_cancel = ov_chord_edge(cancel_mask);
                const bool ov_minus = ov_chord_edge(minus_mask);
                const bool ov_plus = ov_chord_edge(plus_mask);

                bool resume_emulation_on_close = false;
                if (cheat_list_open) {
                    // PromptKeyboard (inside EditCheatFlow) blocks across several frames of its
                    // own via the system keyboard applet - safe to call directly here, unlike
                    // ui_cheats.cpp's equivalent, since this loop never wraps itself in a
                    // GpuCanvas BeginFrame/EndFrame pair (the in-game overlay draws through
                    // Common::IngameOverlay/renderer_vulkan.cpp's own pass instead). The guest
                    // keyboard path (PumpKeyboard, above) already proves swkbdShow is safe to call
                    // from this exact thread while a game is running.
                    const int cheat_total = 1 + SwitchFrontend::CheatCount();
                    if (ov_up && cheat_row > 0) {
                        --cheat_row;
                    }
                    if (ov_down && cheat_row + 1 < cheat_total) {
                        ++cheat_row;
                    }
                    if (ov_confirm) {
                        if (cheat_row == 0) {
                            const int new_index = SwitchFrontend::EditCheatFlow(-1);
                            if (new_index >= 0) {
                                cheat_row = 1 + new_index;
                            }
                        } else {
                            SwitchFrontend::ToggleCheat(cheat_row - 1);
                        }
                    } else if (ov_minus && cheat_row > 0) {
                        SwitchFrontend::EditCheatFlow(cheat_row - 1);
                    } else if (ov_plus && cheat_row > 0) {
                        const int deleted_index = cheat_row - 1;
                        SwitchFrontend::DeleteCheatFlow(deleted_index);
                        const int new_count = SwitchFrontend::CheatCount();
                        cheat_row = new_count > 0 ? 1 + std::min(deleted_index, new_count - 1) : 0;
                    } else if (ov_cancel) {
                        SwitchFrontend::PersistCheats();
                        cheat_list_open = false;
                    }
                } else if (!overlay_armed) {
                    if (ov_up) {
                        overlay_row = move_overlay_row(overlay_row, -1);
                    }
                    if (ov_down) {
                        overlay_row = move_overlay_row(overlay_row, 1);
                    }
                    if (ov_confirm) {
                        if (overlay_row == 0) {
                            overlay_exit_requested = true;
                            overlay_open = false;
                            resume_emulation_on_close = true;
                        } else if (overlay_row == 1) {
                            SwitchFrontend::ResetGameOverridesToLibrary();
                            overlay_settings = SwitchFrontend::GetMenuSettings();
                            SwitchFrontend::RequestFramebufferRelayout();
                        } else if (overlay_row == 2) {
                            // No resume_emulation_on_close here - OpenLayoutEditor() already
                            // paused (a no-op, we already did), and its own CloseLayoutEditor()
                            // resumes emulation once the user's done with the editor itself.
                            overlay_open = false;
                            SwitchFrontend::OpenLayoutEditor();
                        } else if (overlay_row == 3) {
                            cheat_list_open = true;
                            cheat_row = 0;
                        } else {
                            const SwitchFrontend::SettingRow& row = settings_rows[static_cast<std::size_t>(
                                overlay_row - kOverlayActionRowCount)];
                            if (SwitchFrontend::IsBooleanSetting(row.item)) {
                                const SwitchFrontend::MenuSettings before = overlay_settings;
                                SwitchFrontend::ToggleSetting(overlay_settings, row.item);
                                SwitchFrontend::SetMenuSettings(overlay_settings);
                                SwitchFrontend::RequestFramebufferRelayout();
                                SwitchFrontend::CommitMenuSettingsPerGame(before, overlay_settings);
                            } else {
                                overlay_armed = true;
                                overlay_settings_at_arm = overlay_settings;
                            }
                        }
                    }
                    if (ov_cancel) {
                        overlay_open = false;
                        resume_emulation_on_close = true;
                    }
                } else {
                    const SwitchFrontend::SettingRow& row = settings_rows[static_cast<std::size_t>(
                        overlay_row - kOverlayActionRowCount)];
                    if (ov_cancel || ov_confirm) {
                        overlay_armed = false;
                        if (row.item == SwitchFrontend::SettingRowResolution) {
                            SwitchFrontend::ApplyMenuSettings(overlay_settings);
                            SwitchFrontend::RequestFramebufferRelayout();
                            SwitchFrontend::CommitMenuSettingsPerGame(overlay_settings_at_arm,
                                                                      overlay_settings);
                        }
                        // Persist here, once, rather than on every cycle press above
                        // (ApplyMenuSettings only applies live) - see menu_data.h's
                        // ApplyMenuSettings comment for why.
                        SwitchFrontend::SaveConfig();
                    } else {
                        if (ov_left) {
                            SwitchFrontend::CycleSetting(overlay_settings, row.item, -1);
                            if (row.item != SwitchFrontend::SettingRowResolution) {
                                const SwitchFrontend::MenuSettings before = overlay_settings_at_arm;
                                SwitchFrontend::ApplyMenuSettings(overlay_settings);
                                SwitchFrontend::RequestFramebufferRelayout();
                                SwitchFrontend::CommitMenuSettingsPerGame(before, overlay_settings);
                                overlay_settings_at_arm = overlay_settings;
                            }
                        }
                        if (ov_right) {
                            SwitchFrontend::CycleSetting(overlay_settings, row.item, 1);
                            if (row.item != SwitchFrontend::SettingRowResolution) {
                                const SwitchFrontend::MenuSettings before = overlay_settings_at_arm;
                                SwitchFrontend::ApplyMenuSettings(overlay_settings);
                                SwitchFrontend::RequestFramebufferRelayout();
                                SwitchFrontend::CommitMenuSettingsPerGame(before, overlay_settings);
                                overlay_settings_at_arm = overlay_settings;
                            }
                        }
                    }
                }

                if (overlay_open) {
                    Common::IngameOverlay::SetState(
                        cheat_list_open ? BuildCheatOverlayState(cheat_row)
                                       : BuildIngameOverlayState(settings_rows, overlay_row,
                                                                 overlay_armed));
                    prev_held = held;
                    svcSleepThread(4'000'000);
                    continue;
                }
                Common::IngameOverlay::SetState({});
                if (resume_emulation_on_close) {
                    SwitchFrontend::ResumeEmulation();
                }
            }

            const u64 mirror_mask =
                SwitchFrontend::GetMenuActionButtons(SwitchFrontend::MenuAction::MirrorScreen);
            const bool mirror_edge = !editor_open && mirror_mask != 0 &&
                                     (held & mirror_mask) == mirror_mask &&
                                     (prev_held & mirror_mask) != mirror_mask;
            if (mirror_edge) {
                SwitchFrontend::MirrorScreenSides();
            }

            if (!editor_open) {
                SwitchFrontend::UpdateInput(mirror_edge ? SwitchFrontend::InputState{} : state);
            }

            if (!editor_open) {
                const u64 cycle_layout_mask = SwitchFrontend::GetMenuActionButtons(
                    SwitchFrontend::MenuAction::CycleLayout);
                const bool cycle_layout_edge = cycle_layout_mask != 0 &&
                                               (held & cycle_layout_mask) == cycle_layout_mask &&
                                               (prev_held & cycle_layout_mask) != cycle_layout_mask;
                if (cycle_layout_edge) {
                    SwitchFrontend::CycleScreenLayout();
                }
                const u64 toggle_pointer_mask = SwitchFrontend::GetMenuActionButtons(
                    SwitchFrontend::MenuAction::TogglePointer);
                // If TogglePointer's binding is (or overlaps) a subset of the mirror-screen chord
                // and the whole chord is currently held, the player is going for the chord, not a
                // bare TogglePointer press - suppress it so holding e.g. L3+Minus doesn't also
                // toggle the pointer on the way to completing the chord.
                const bool suppress_for_mirror_chord =
                    mirror_mask != 0 && (toggle_pointer_mask & mirror_mask) == toggle_pointer_mask &&
                    (held & mirror_mask) == mirror_mask;
                const bool toggle_pointer_edge =
                    toggle_pointer_mask != 0 &&
                    (held & toggle_pointer_mask) == toggle_pointer_mask &&
                    (prev_held & toggle_pointer_mask) != toggle_pointer_mask;
                if (toggle_pointer_edge && !suppress_for_mirror_chord) {
                    SwitchFrontend::TogglePointerMode();
                }
            }

            prev_held = held;
            if (!SwitchFrontend::IsRunning()) {
                break;
            }
            if (overlay_exit_requested) {
                break;
            }
            svcSleepThread(4'000'000);
        }
    }

    return ExitInGameToLibrary(rom, session_start_tick, model);
}

void DebugLogUpdateStaging(const std::string& line) {
    if (FILE* f = std::fopen("sdmc:/switch/azahar/update_staging_debug.log", "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fclose(f);
    }
}

} // namespace

namespace Common::Log {
void SetColorConsoleBackendEnabled(bool enabled);
}

int main(int argc, char* argv[]) {
    SwitchFrontend::SetOwnNroPath((argc > 0 && argv[0] != nullptr) ? argv[0] : std::string{});

    const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
    if (have_socket) {
        nxlinkStdio();
    }
    const bool have_romfs = R_SUCCEEDED(romfsInit());
    if (!have_romfs) {
        std::printf("Warning: romfsInit() failed.\n");
    }
    if (!Common::Horizon::PinCurrentThread(0)) {
        std::printf("Warning: failed to pin frontend thread to core 0.\n");
    }

    const int launch_count = SwitchFrontend::Bootstrap();
    Common::Log::SetColorConsoleBackendEnabled(have_socket);
    std::printf("FS & logging up (launch #%d). Logs are located at sdmc:/switch/azahar/log/\n",
                launch_count);
    SwitchFrontend::ApplySavedStorageOverride();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    StartSixAxis();

    SwitchFrontend::InitializeInput();
    SwitchFrontend::LoadMenuBindings();
    SwitchFrontend::LoadTitleDatabase();
    SwitchFrontend::LoadTranslations();
    SwitchFrontend::SetUiLanguage(SwitchFrontend::GetMenuSettings().language);

    std::string pending_rom = (argc > 1 && argv[1] != nullptr) ? argv[1] : std::string{};

    SwitchFrontend::UiModel model;

    while (appletMainLoop()) {
        std::string rom;
        if (!pending_rom.empty()) {
            rom = std::move(pending_rom);
            pending_rom.clear();
            model.app_state = SwitchFrontend::AppState::Booting;
        } else {
            rom = RunLauncher(pad, model);
            if (model.exit_requested) {
                break;
            }
        }

        if (!rom.empty() && RunGame(pad, rom, model)) {
            break;
        }
    }

    SwitchFrontend::ShutdownInput();
    StopSixAxis();
    SwitchFrontend::Shutdown();
    if (have_romfs) {
        romfsExit();
    }
    if (have_socket) {
        socketExit();
    }
    return 0;
}

namespace SwitchFrontend {

void UnmountRomfsForSelfReplace() {
    romfsExit();
}

void RemountRomfsAfterFailedSelfReplace() {
    if (R_FAILED(romfsInit())) {
        std::printf("Warning: romfsInit() failed on remount after a failed self-replace.\n");
    }
}

[[noreturn]] void RelaunchInto(const std::string& path) {
    DebugLogUpdateStaging("RelaunchInto(\"" + path + "\")");
    const Result commit_rc = fsdevCommitDevice("sdmc");
    if (R_FAILED(commit_rc)) {
        char commit_line[64];
        std::snprintf(commit_line, sizeof(commit_line), "fsdevCommitDevice failed: 0x%08X",
                      commit_rc);
        DebugLogUpdateStaging(commit_line);
    }
    if (!path.empty()) {
        const Result rc = envSetNextLoad(path.c_str(), path.c_str());
        char result_line[64];
        std::snprintf(result_line, sizeof(result_line), "envSetNextLoad result: 0x%08X", rc);
        DebugLogUpdateStaging(result_line);
        if (R_FAILED(rc)) {
            std::printf("envSetNextLoad(\"%s\") failed: 0x%08X\n", path.c_str(), rc);
        }
    }
    SwitchFrontend::Shutdown();
    std::_Exit(0);
}

[[noreturn]] void RelaunchSelf() {
    RelaunchInto(HasOwnNroPath() ? GetOwnNroPath() : std::string{});
}

} // namespace SwitchFrontend
