// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <switch.h>

#include "citra_switch/applets/swkbd.h"
#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/menu.h"
#include "common/horizon_thread.h"

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;
}

namespace {

constexpr std::array<std::pair<u64, SwitchFrontend::InputButton>, 14> button_map{{
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
}};

// Each controller style reports its six-axis sensor through a handle of its own.
// Sideways Joy-Con is disabled because it's more work and I don't see a big need for it.
// The emulator doesn't have support for button re-mapping yet anyways.
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

SwitchFrontend::MotionState PollMotion(PadState& pad) {
    const u64 style_set = padGetStyleSet(&pad);
    HidSixAxisSensorState sensor{};
    bool read = false;

    if ((style_set & HidNpadStyleTag_NpadHandheld) != 0) {
        read = hidGetSixAxisSensorStates(six_axis_handles[0], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadFullKey) != 0) {
        read = hidGetSixAxisSensorStates(six_axis_handles[1], &sensor, 1) > 0;
    } else if ((style_set & HidNpadStyleTag_NpadJoyDual) != 0) {
        // A dual pair reports through whichever Joy-Con is actually attached.
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

u64 PollInput(PadState& pad) {
    padUpdate(&pad);
    const u64 held = padGetButtons(&pad);
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);

    SwitchFrontend::InputState state{
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
    if (hidGetTouchScreenStates(&touch, 1) != 0 && touch.count > 0) {
        state.touch_pressed = true;
        state.touch_x = touch.touches[0].x;
        state.touch_y = touch.touches[0].y;
    }
    SwitchFrontend::UpdateInput(state);
    return held;
}

bool ReturnToMenuRequested(u64 held) {
    constexpr u64 chord = HidNpadButton_Plus | HidNpadButton_Minus;
    return (held & chord) == chord;
}

void RunGame(PadState& pad, const std::string& rom) {
    if (!SwitchFrontend::CreateWindow(nwindowGetDefault())) {
        std::printf("EmuWindow no worky.\n");
        SwitchFrontend::SetMenuNotice("Couldn't create the render window");
        return;
    }

    if (SwitchFrontend::BootRom(rom)) {
        u64 prev_held = 0;
        while (appletMainLoop()) {
            // Blocks while the system keyboard is up. The emulation thread is waiting on it, so
            // nothing is being drawn meanwhile.
            SwitchFrontend::PumpKeyboard();
            const u64 held = PollInput(pad);
            if (ReturnToMenuRequested(held)) {
                break;
            }
            // Clicking the right stick (R3) cycles through the screen layouts.
            if ((held & HidNpadButton_StickR) != 0 && (prev_held & HidNpadButton_StickR) == 0) {
                SwitchFrontend::CycleScreenLayout();
            }
            prev_held = held;
            if (!SwitchFrontend::IsRunning()) {
                break;
            }
            svcSleepThread(1'000'000);
        }
        SwitchFrontend::StopRom();
        if (SwitchFrontend::LoadFailed()) {
            SwitchFrontend::SetMenuNotice("Couldn't launch — check keys / ROM");
        }
    } else {
        SwitchFrontend::SetMenuNotice("Couldn't launch — ROM not loadable");
    }

    SwitchFrontend::DestroyWindow();
}

} // namespace

int main(int argc, char* argv[]) {
    const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
    if (have_socket) {
        nxlinkStdio();
    }
    // Mount the embedded romfs for deko3D shaders
    const bool have_romfs = R_SUCCEEDED(romfsInit());
    if (!have_romfs) {
        std::printf("Warning: romfsInit() failed.\n");
    }
    if (!Common::Horizon::PinCurrentThread(0)) {
        std::printf("Warning: failed to pin frontend thread to core 0.\n");
    }

    // Resolve SD-card dirs and create folders/files if not present
    const int launch_count = SwitchFrontend::Bootstrap();
    std::printf("FS & logging up (launch #%d). Logs are located at sdmc:/switch/dekopon/log/\n",
                launch_count);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    StartSixAxis();

    SwitchFrontend::InitializeInput();

    std::string pending_rom = (argc > 1 && argv[1] != nullptr) ? argv[1] : std::string{};

    while (appletMainLoop()) {
        std::string rom;
        if (!pending_rom.empty()) {
            rom = std::move(pending_rom);
            pending_rom.clear();
        } else {
            const SwitchFrontend::MenuResult choice = SwitchFrontend::RunMenu(pad);
            if (choice.action == SwitchFrontend::MenuAction::Exit) {
                break;
            }
            rom = choice.path;
        }

        if (!rom.empty()) {
            RunGame(pad, rom);
        }
    }

    SwitchFrontend::ShutdownMenu();
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
