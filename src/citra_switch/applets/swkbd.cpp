// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <fmt/format.h>

#include "citra_switch/applets/swkbd.h"
#include "citra_switch/applets/swkbd_horizon.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/applets/swkbd.h"

namespace SwitchFrontend {

namespace {

/// libctru indexes button text by on-screen position, and the confirm button is always the
/// right-most one whatever the button count.
constexpr std::size_t OK_TEXT_INDEX = 2;

/// The 3DS reports the cancel button as the left-most one.
constexpr u8 CANCEL_BUTTON = 0;

/// The button index the application expects on confirm.
u8 OkButton(const Frontend::KeyboardConfig& config) {
    if (config.button_config == Frontend::ButtonConfig::None) {
        return 0;
    }
    return static_cast<u8>(config.button_config);
}

HorizonKeyboardRequest::Layout ToLayout(Frontend::KeyboardType type) {
    switch (type) {
    case Frontend::KeyboardType::QWERTY:
        return HorizonKeyboardRequest::Layout::QWERTY;
    case Frontend::KeyboardType::NumPad:
        return HorizonKeyboardRequest::Layout::NumPad;
    case Frontend::KeyboardType::Western:
        return HorizonKeyboardRequest::Layout::Latin;
    case Frontend::KeyboardType::Normal:
    default:
        return HorizonKeyboardRequest::Layout::Normal;
    }
}

u32 MinLength(const Frontend::KeyboardConfig& config) {
    switch (config.accept_mode) {
    case Frontend::AcceptedInput::FixedLength:
        return config.max_text_length;
    case Frontend::AcceptedInput::NotEmpty:
    case Frontend::AcceptedInput::NotEmptyAndNotBlank:
        return 1;
    default:
        // NotBlank still accepts empty input, so it cannot use a minimum length.
        return 0;
    }
}

std::string ValidationMessage(Frontend::ValidationError error,
                              const Frontend::KeyboardConfig& config) {
    switch (error) {
    case Frontend::ValidationError::FixedLengthRequired:
        return fmt::format("Text must be exactly {} characters long", config.max_text_length);
    case Frontend::ValidationError::MaxLengthExceeded:
        return fmt::format("Text must be no more than {} characters long", config.max_text_length);
    case Frontend::ValidationError::MaxDigitsExceeded:
        return fmt::format("Text must contain no more than {} digits", config.max_digits);
    case Frontend::ValidationError::BlankInputNotAllowed:
        return "Blank input is not allowed";
    case Frontend::ValidationError::EmptyInputNotAllowed:
        return "Empty input is not allowed";
    case Frontend::ValidationError::AtSignNotAllowed:
        return "The @ sign is not allowed";
    case Frontend::ValidationError::PercentNotAllowed:
        return "The % sign is not allowed";
    case Frontend::ValidationError::BackslashNotAllowed:
        return "The \\ sign is not allowed";
    case Frontend::ValidationError::ProfanityNotAllowed:
        return "Profanity is not allowed";
    default:
        return "That input is not accepted";
    }
}

// Software keyboard backed by the Horizon swkbd library applet.
class SwitchKeyboard final : public Frontend::SoftwareKeyboard {
public:
    void Execute(const Frontend::KeyboardConfig& config) override;
    void ShowError(const std::string& error) override;

private:
    /// Message from the application's filter callback, shown on the keyboard it reopens.
    std::string pending_error;
};

struct Request {
    Frontend::SoftwareKeyboard* keyboard;
    Frontend::KeyboardConfig config;
    std::string header;
};

struct Reply {
    std::string text;
    u8 button;
};

std::mutex mutex;
std::condition_variable cv;
std::optional<Request> request;
std::optional<Reply> reply;
bool cancelled = false;

Reply ShowKeyboard(const Request& req) {
    const Frontend::KeyboardConfig& config = req.config;

    HorizonKeyboardRequest horizon;
    horizon.layout = ToLayout(config.type);
    horizon.conceal_text = config.password_mode != Frontend::PasswordMode::None;
    horizon.allow_newlines = config.multiline_mode;
    horizon.disable_at = config.filters.prevent_at;
    horizon.disable_percent = config.filters.prevent_percent;
    horizon.disable_backslash = config.filters.prevent_backslash;
    // prevent_digit caps how many digits are allowed rather than banning them, so only take the
    // number keys away when none are allowed at all.
    horizon.disable_numbers = config.filters.prevent_digit && config.max_digits == 0;
    horizon.max_length = config.max_text_length;
    horizon.min_length = MinLength(config);
    horizon.header = req.header;
    horizon.guide = config.hint_text;
    horizon.ok_text =
        config.button_text.size() > OK_TEXT_INDEX && !config.button_text[OK_TEXT_INDEX].empty()
            ? config.button_text[OK_TEXT_INDEX]
            : Frontend::SWKBD_BUTTON_OKAY;
    // Run the application's own rules inside the applet, so a confirmed string always survives Finalize().
    horizon.validate = [keyboard = req.keyboard, &config](const std::string& text) {
        const Frontend::ValidationError error = keyboard->ValidateInput(text);
        return error == Frontend::ValidationError::None ? std::string{}
                                                        : ValidationMessage(error, config);
    };

    std::string text;
    switch (ShowHorizonKeyboard(horizon, &text)) {
    case HorizonKeyboardResult::Confirmed:
        return {std::move(text), OkButton(config)};
    case HorizonKeyboardResult::Failed:
        LOG_ERROR(Applet_SWKBD, "Could not launch the system keyboard");
        return {std::string{}, CANCEL_BUTTON};
    case HorizonKeyboardResult::Dismissed:
    default:
        LOG_INFO(Applet_SWKBD, "Keyboard closed without input");
        return {std::string{}, CANCEL_BUTTON};
    }
}

void SwitchKeyboard::Execute(const Frontend::KeyboardConfig& config_) {
    Frontend::SoftwareKeyboard::Execute(config_);

    const std::string header = std::exchange(pending_error, std::string{});

    // A keyboard with no cancel button has no way to tell the application it was dismissed, so keep
    // asking until Finalize() accepts the input.
    while (true) {
        std::unique_lock lock{mutex};
        if (cancelled) {
            break;
        }
        request = Request{this, config, header};
        reply.reset();
        cv.notify_all();
        cv.wait(lock, [] { return reply.has_value() || cancelled; });
        if (!reply.has_value()) {
            request.reset();
            break;
        }
        const Reply result = std::move(*reply);
        reply.reset();
        lock.unlock();

        if (Finalize(result.text, result.button) == Frontend::ValidationError::None) {
            return;
        }
        // The validator already vetted anything the user confirmed, so this only trips when the
        // application offered no cancel button and the user backed out anyway.
        LOG_INFO(Applet_SWKBD, "Keyboard dismissed but the application requires input");
    }

    LOG_INFO(Applet_SWKBD, "Keyboard cancelled because emulation is stopping");
    Finalize(std::string{}, CANCEL_BUTTON);
}

void SwitchKeyboard::ShowError(const std::string& error) {
    LOG_WARNING(Applet_SWKBD, "Application rejected the keyboard input: {}", error);
    pending_error = error;
}

} // namespace

void RegisterKeyboard(Core::System& system) {
    {
        std::lock_guard lock{mutex};
        cancelled = false;
        request.reset();
        reply.reset();
    }
    system.RegisterSoftwareKeyboard(std::make_shared<SwitchKeyboard>());
}

void PumpKeyboard() {
    Request req;
    {
        std::lock_guard lock{mutex};
        if (!request.has_value()) {
            return;
        }
        req = std::move(*request);
        request.reset();
    }

    Reply result = ShowKeyboard(req);

    std::lock_guard lock{mutex};
    reply = std::move(result);
    cv.notify_all();
}

void CancelKeyboard() {
    std::lock_guard lock{mutex};
    cancelled = true;
    request.reset();
    cv.notify_all();
}

} // namespace SwitchFrontend
