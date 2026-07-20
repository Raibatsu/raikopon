// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com) & Raibatsu (hello@raibatsu.com) (added my name since its an original file but I'm not sure about the legality thingy. im a dev, not a lawyer)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace SwitchFrontend {

// Shows the system keyboard and returns what the user typed, or `initial` unchanged if they
// cancel. The keyboard's Return key inserts a newline rather than submitting, so this also
// works for multi-line input (e.g. cheat code entry).
std::string PromptKeyboard(const std::string& header, const std::string& guide,
                            const std::string& initial, int max_len);

} // namespace SwitchFrontend
