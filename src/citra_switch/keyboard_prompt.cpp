// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/keyboard_prompt.h"

#include <algorithm>
#include <cstddef>
#include <vector>
#include <switch.h>

namespace SwitchFrontend {

namespace {
constexpr std::size_t kMinBufferSize = 1024;
} // namespace

std::string PromptKeyboard(const std::string& header, const std::string& guide,
                           const std::string& initial, int max_len) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return initial;
    }

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(max_len));
    if (!header.empty()) {
        swkbdConfigSetHeaderText(&kbd, header.c_str());
    }
    if (!guide.empty()) {
        swkbdConfigSetGuideText(&kbd, guide.c_str());
    }
    if (!initial.empty()) {
        swkbdConfigSetInitialText(&kbd, initial.c_str());
    }

    std::vector<char> out(
        std::max<std::size_t>(kMinBufferSize, static_cast<std::size_t>(max_len) * 3 + 1), '\0');
    const Result rc = swkbdShow(&kbd, out.data(), out.size());
    swkbdClose(&kbd);

    if (R_FAILED(rc)) {
        return "";
    }
    return std::string(out.data());
}

} // namespace SwitchFrontend
