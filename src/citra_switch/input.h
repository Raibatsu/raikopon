// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace SwitchFrontend {

enum class InputButton : std::uint8_t {
    A,
    B,
    X,
    Y,
    Up,
    Down,
    Left,
    Right,
    L,
    R,
    Start,
    Select,
    ZL,
    ZR,
};

constexpr std::uint64_t ButtonMask(InputButton button) {
    return std::uint64_t{1} << static_cast<std::uint8_t>(button);
}

// A six-axis sample nicely borrowed (read stolen) from libnx.
struct MotionState {
    bool active{}; /// False when the current controller has no readable sensor
    float accel_x{};
    float accel_y{};
    float accel_z{};
    float gyro_x{};
    float gyro_y{};
    float gyro_z{};
};

struct InputState {
    std::uint64_t buttons{};
    std::int32_t left_x{};
    std::int32_t left_y{};
    std::int32_t right_x{};
    std::int32_t right_y{};
    bool touch_pressed{};
    std::uint32_t touch_x{};
    std::uint32_t touch_y{};
    MotionState motion{};
};

// What drives the on-screen touch pointer while pointer mode is on.
enum class PointerSource : std::uint8_t {
    Stick,
    Gyro,
};

// The bottom-screen pointer position as a fraction of the bottom screen, for the crosshair.
struct PointerCursor {
    bool active{};
    float frac_x{}; // 0 at the left edge, 1 at the right edge.
    float frac_y{}; // 0 at the top edge, 1 at the bottom edge.
};

// Registers the Switch controller and sets up the default 3DS bindings.
void InitializeInput();

// Pushes the latest libnx input to the emulation thread.
void UpdateInput(const InputState& state);

// Releases all input and unregisters the Switch controller.
void ShutdownInput();

// The configured pointer driver.
PointerSource GetPointerSource();
void SetPointerSource(PointerSource source);

// Pointer mode lets the stick/gyro drive the 3DS touchscreen with ZL/ZR as the tap.
bool IsPointerModeActive();
void TogglePointerMode();

// Disables pointer mode and recenters the pointer.
void ResetPointer();

// The current crosshair position, queried by the emu window for the renderer.
PointerCursor GetPointerCursor();

} // namespace SwitchFrontend
