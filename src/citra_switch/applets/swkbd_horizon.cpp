// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>
#include <switch.h>

#include "citra_switch/applets/swkbd_horizon.h"

namespace SwitchFrontend {

namespace {

/// Enough room for an error message.
constexpr std::size_t MIN_BUFFER_SIZE = 1024;

/// Reachable from the TextCheck callback, which has nowhere to hang user data.
const HorizonKeyboardRequest* active_request = nullptr;

SwkbdType ToSwkbdType(HorizonKeyboardRequest::Layout layout) {
    switch (layout) {
    case HorizonKeyboardRequest::Layout::QWERTY:
        return SwkbdType_QWERTY;
    case HorizonKeyboardRequest::Layout::NumPad:
        return SwkbdType_NumPad;
    case HorizonKeyboardRequest::Layout::Latin:
        return SwkbdType_Latin;
    case HorizonKeyboardRequest::Layout::Normal:
    default:
        return SwkbdType_Normal;
    }
}

u32 ToKeyDisableBitmask(const HorizonKeyboardRequest& request) {
    u32 mask = 0;
    if (request.disable_at) {
        mask |= SwkbdKeyDisableBitmask_At;
    }
    if (request.disable_percent) {
        mask |= SwkbdKeyDisableBitmask_Percent;
    }
    if (request.disable_backslash) {
        mask |= SwkbdKeyDisableBitmask_Backslash;
    }
    if (request.disable_numbers) {
        mask |= SwkbdKeyDisableBitmask_Numbers;
    }
    return mask;
}

SwkbdTextCheckResult TextCheckCallback(char* text, std::size_t text_size) {
    if (active_request == nullptr || !active_request->validate) {
        return SwkbdTextCheckResult_OK;
    }
    const std::string error = active_request->validate(text);
    if (error.empty()) {
        return SwkbdTextCheckResult_OK;
    }
    std::snprintf(text, text_size, "%s", error.c_str());
    return SwkbdTextCheckResult_Bad;
}

} // namespace

HorizonKeyboardResult ShowHorizonKeyboard(const HorizonKeyboardRequest& request,
                                          std::string* out_text) {
    out_text->clear();

    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return HorizonKeyboardResult::Failed;
    }

    if (request.conceal_text) {
        swkbdConfigMakePresetPassword(&kbd);
    } else {
        swkbdConfigMakePresetDefault(&kbd);
    }

    swkbdConfigSetType(&kbd, ToSwkbdType(request.layout));
    swkbdConfigSetKeySetDisableBitmask(&kbd, ToKeyDisableBitmask(request));
    swkbdConfigSetStringLenMax(&kbd, request.max_length);
    swkbdConfigSetStringLenMin(&kbd, request.min_length);
    swkbdConfigSetReturnButtonFlag(&kbd, request.allow_newlines ? 1 : 0);

    if (!request.header.empty()) {
        swkbdConfigSetHeaderText(&kbd, request.header.c_str());
    }
    if (!request.guide.empty()) {
        swkbdConfigSetGuideText(&kbd, request.guide.c_str());
    }
    if (!request.ok_text.empty()) {
        swkbdConfigSetOkButtonText(&kbd, request.ok_text.c_str());
    }

    active_request = &request;
    swkbdConfigSetTextCheckCallback(&kbd, TextCheckCallback);

    // swkbd hands the TextCheck callback the same buffer it writes the result into, so it has to fit either.
    std::vector<char> out(std::max<std::size_t>(MIN_BUFFER_SIZE, request.max_length * 3 + 1), '\0');
    const Result rc = swkbdShow(&kbd, out.data(), out.size());

    active_request = nullptr;
    swkbdClose(&kbd);

    if (R_FAILED(rc)) {
        return HorizonKeyboardResult::Dismissed;
    }

    *out_text = out.data();
    return HorizonKeyboardResult::Confirmed;
}

} // namespace SwitchFrontend
