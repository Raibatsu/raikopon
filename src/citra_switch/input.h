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

struct InputState {
    std::uint64_t buttons{};
    std::int32_t left_x{};
    std::int32_t left_y{};
    std::int32_t right_x{};
    std::int32_t right_y{};
    bool touch_pressed{};
    std::uint32_t touch_x{};
    std::uint32_t touch_y{};
};

// Registers the Switch controller and sets up the default 3DS bindings.
void InitializeInput();

// Pushes the latest libnx input to the emulation thread.
void UpdateInput(const InputState& state);

// Releases all input and unregisters the Switch controller.
void ShutdownInput();

} // namespace SwitchFrontend
