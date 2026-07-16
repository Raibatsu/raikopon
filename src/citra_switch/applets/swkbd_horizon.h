// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace SwitchFrontend {

/// A keyboard described in terms of the Horizon swkbd applet.
struct HorizonKeyboardRequest {
    enum class Layout : std::uint8_t {
        Normal, /// Several pages (QWERTY/accents/symbols)
        QWERTY,
        NumPad,
        Latin, /// Latin scripts only
    };

    Layout layout{Layout::Normal};
    bool conceal_text{};    /// Password field
    bool allow_newlines{};  /// Give the return key a newline rather than nothing
    bool disable_at{};      /// Take the '@' key away
    bool disable_percent{}; /// Take the '%' key away
    bool disable_backslash{};
    bool disable_numbers{};
    std::uint32_t max_length{};
    std::uint32_t min_length{};
    std::string header;  /// Shown above the field
    std::string guide;   /// Placeholder shown while the field is empty
    std::string ok_text; /// Label for the confirm button

    /// Vets the string when the user confirms.
    std::function<std::string(const std::string&)> validate;
};

enum class HorizonKeyboardResult : std::uint8_t {
    Confirmed, /// out_text holds the user's input
    Dismissed, /// The user backed out
    Failed,    /// The applet could not be launched
};

/// Shows the system keyboard and blocks until it closes. Must be called from the thread that owns appletMainLoop().
HorizonKeyboardResult ShowHorizonKeyboard(const HorizonKeyboardRequest& request,
                                          std::string* out_text);

} // namespace SwitchFrontend
