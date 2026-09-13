// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

namespace SwitchFrontend {

struct WhatsNextItem {
    std::string text;
    bool done = false;
};

void EnsureWhatsNextLoaded();
bool IsWhatsNextReady();
const std::vector<WhatsNextItem>& WhatsNextItems();

} // namespace SwitchFrontend
