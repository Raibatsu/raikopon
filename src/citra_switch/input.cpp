// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <tuple>

#include "citra_switch/emu_window.h"
#include "citra_switch/input.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "core/frontend/input.h"

// Button mappings and inputs for the Switch
namespace SwitchFrontend {
namespace {

constexpr float kStickRange = 32767.0f;
constexpr float kStickDeadzone = 0.15f;

// libnx reports angular velocity in rotations/sec vs the 3DS gyroscope in deg/sec.
constexpr float kRotationsToDegrees = 360.0f;

// What the 3DS accelerometer reads while lying face-up, which is the orientation a Switch held upright maps onto.
constexpr Common::Vec3<float> kRestAccel{0.0f, -1.0f, 0.0f};

std::atomic<std::uint64_t> s_buttons{};
std::array<std::atomic<float>, Settings::NativeAnalog::NumAnalogs> s_stick_x{};
std::array<std::atomic<float>, Settings::NativeAnalog::NumAnalogs> s_stick_y{};
std::array<std::atomic<float>, 3> s_accel{};
std::array<std::atomic<float>, 3> s_gyro{};
bool s_touch_active{};

class SwitchButton final : public Input::ButtonDevice {
public:
    explicit SwitchButton(InputButton button_) : button{button_} {}

    bool GetStatus() const override {
        return (s_buttons.load(std::memory_order_relaxed) & ButtonMask(button)) != 0;
    }

private:
    InputButton button;
};

class SwitchAnalog final : public Input::AnalogDevice {
public:
    explicit SwitchAnalog(std::size_t analog_) : analog{analog_} {}

    std::tuple<float, float> GetStatus() const override {
        return {s_stick_x[analog].load(std::memory_order_relaxed),
                s_stick_y[analog].load(std::memory_order_relaxed)};
    }

private:
    std::size_t analog;
};

class SwitchMotion final : public Input::MotionDevice {
public:
    std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetStatus() const override {
        return {Load(s_accel), Load(s_gyro)};
    }

private:
    static Common::Vec3<float> Load(const std::array<std::atomic<float>, 3>& source) {
        return {source[0].load(std::memory_order_relaxed),
                source[1].load(std::memory_order_relaxed),
                source[2].load(std::memory_order_relaxed)};
    }
};

class SwitchButtonFactory final : public Input::Factory<Input::ButtonDevice> {
public:
    std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override {
        const int button =
            std::clamp(params.Get("button", 0), 0, static_cast<int>(InputButton::ZR));
        return std::make_unique<SwitchButton>(static_cast<InputButton>(button));
    }
};

class SwitchAnalogFactory final : public Input::Factory<Input::AnalogDevice> {
public:
    std::unique_ptr<Input::AnalogDevice> Create(const Common::ParamPackage& params) override {
        const int analog =
            std::clamp(params.Get("analog", 0), 0, Settings::NativeAnalog::NumAnalogs - 1);
        return std::make_unique<SwitchAnalog>(static_cast<std::size_t>(analog));
    }
};

class SwitchMotionFactory final : public Input::Factory<Input::MotionDevice> {
public:
    std::unique_ptr<Input::MotionDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<SwitchMotion>();
    }
};

std::string ButtonParam(InputButton button) {
    return Common::ParamPackage{
        {"engine", "switch"},
        {"button", std::to_string(static_cast<int>(button))},
    }
        .Serialize();
}

std::string AnalogParam(Settings::NativeAnalog::Values analog) {
    return Common::ParamPackage{
        {"engine", "switch"},
        {"analog", std::to_string(static_cast<int>(analog))},
    }
        .Serialize();
}

std::string MotionParam() {
    return Common::ParamPackage{{"engine", "switch"}}.Serialize();
}

// The Switch and 3DS sensor frames are one rotation apart: the 3DS's x+ (left), y+ (out of the
// touch screen) and z+ (up) read off the Switch's -x, z and y respectively.
Common::Vec3<float> ToConsoleFrame(float x, float y, float z) {
    return {-x, z, y};
}

void StoreMotion(const Common::Vec3<float>& accel, const Common::Vec3<float>& gyro) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
        s_accel[axis].store(accel[axis], std::memory_order_relaxed);
        s_gyro[axis].store(gyro[axis], std::memory_order_relaxed);
    }
}

std::tuple<float, float> NormalizeStick(std::int32_t raw_x, std::int32_t raw_y) {
    const float x = std::clamp(static_cast<float>(raw_x) / kStickRange, -1.0f, 1.0f);
    const float y = std::clamp(static_cast<float>(raw_y) / kStickRange, -1.0f, 1.0f);
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= kStickDeadzone) {
        return {0.0f, 0.0f};
    }

    const float scaled_magnitude =
        (std::min(magnitude, 1.0f) - kStickDeadzone) / (1.0f - kStickDeadzone);
    return {x / magnitude * scaled_magnitude, y / magnitude * scaled_magnitude};
}

void SetDefaultBindings() {
    auto& profile = Settings::values.current_input_profile;
    profile.name = "Nintendo Switch";

    profile.buttons.fill("engine:null");
    profile.buttons[Settings::NativeButton::A] = ButtonParam(InputButton::A);
    profile.buttons[Settings::NativeButton::B] = ButtonParam(InputButton::B);
    profile.buttons[Settings::NativeButton::X] = ButtonParam(InputButton::X);
    profile.buttons[Settings::NativeButton::Y] = ButtonParam(InputButton::Y);
    profile.buttons[Settings::NativeButton::Up] = ButtonParam(InputButton::Up);
    profile.buttons[Settings::NativeButton::Down] = ButtonParam(InputButton::Down);
    profile.buttons[Settings::NativeButton::Left] = ButtonParam(InputButton::Left);
    profile.buttons[Settings::NativeButton::Right] = ButtonParam(InputButton::Right);
    profile.buttons[Settings::NativeButton::L] = ButtonParam(InputButton::L);
    profile.buttons[Settings::NativeButton::R] = ButtonParam(InputButton::R);
    profile.buttons[Settings::NativeButton::Start] = ButtonParam(InputButton::Start);
    profile.buttons[Settings::NativeButton::Select] = ButtonParam(InputButton::Select);
    profile.buttons[Settings::NativeButton::ZL] = ButtonParam(InputButton::ZL);
    profile.buttons[Settings::NativeButton::ZR] = ButtonParam(InputButton::ZR);

    profile.analogs[Settings::NativeAnalog::CirclePad] =
        AnalogParam(Settings::NativeAnalog::CirclePad);
    profile.analogs[Settings::NativeAnalog::CStick] = AnalogParam(Settings::NativeAnalog::CStick);
    profile.motion_device = MotionParam();
    profile.touch_device = "engine:emu_window";
    profile.controller_touch_device.clear();
    profile.use_touchpad = false;
    profile.use_touch_from_button = false;
}

} // namespace

void InitializeInput() {
    Input::RegisterFactory<Input::ButtonDevice>("switch", std::make_shared<SwitchButtonFactory>());
    Input::RegisterFactory<Input::AnalogDevice>("switch", std::make_shared<SwitchAnalogFactory>());
    Input::RegisterFactory<Input::MotionDevice>("switch", std::make_shared<SwitchMotionFactory>());
    StoreMotion(kRestAccel, {});
    SetDefaultBindings();
}

void UpdateInput(const InputState& state) {
    const auto [left_x, left_y] = NormalizeStick(state.left_x, state.left_y);
    const auto [right_x, right_y] = NormalizeStick(state.right_x, state.right_y);

    s_buttons.store(state.buttons, std::memory_order_relaxed);
    s_stick_x[Settings::NativeAnalog::CirclePad].store(left_x, std::memory_order_relaxed);
    s_stick_y[Settings::NativeAnalog::CirclePad].store(left_y, std::memory_order_relaxed);
    s_stick_x[Settings::NativeAnalog::CStick].store(right_x, std::memory_order_relaxed);
    s_stick_y[Settings::NativeAnalog::CStick].store(right_y, std::memory_order_relaxed);

    if (state.motion.active) {
        StoreMotion(
            ToConsoleFrame(state.motion.accel_x, state.motion.accel_y, state.motion.accel_z),
            ToConsoleFrame(state.motion.gyro_x, state.motion.gyro_y, state.motion.gyro_z) *
                kRotationsToDegrees);
    } else {
        StoreMotion(kRestAccel, {});
    }

    EmuWindow_Switch* window = GetEmuWindow();
    if (!window) {
        s_touch_active = false;
        return;
    }
    if (state.touch_pressed) {
        if (s_touch_active) {
            window->TouchMoved(state.touch_x, state.touch_y);
        } else {
            s_touch_active = window->TouchPressed(state.touch_x, state.touch_y);
        }
    } else if (s_touch_active) {
        window->TouchReleased();
        s_touch_active = false;
    }
}

void ShutdownInput() {
    UpdateInput({});
    Input::UnregisterFactory<Input::ButtonDevice>("switch");
    Input::UnregisterFactory<Input::AnalogDevice>("switch");
    Input::UnregisterFactory<Input::MotionDevice>("switch");
    Input::UnregisterFactory<Input::TouchDevice>("emu_window");
}

} // namespace SwitchFrontend
