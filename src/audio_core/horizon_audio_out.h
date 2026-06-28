// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace AudioCore::Horizon {

struct AudioOut;
using FillCallback = void (*)(void* user_data, std::int16_t* output, std::size_t frames);

AudioOut* CreateAudioOut(std::uint32_t& result);
std::uint32_t StartAudioOut(AudioOut* audio_out, FillCallback callback, void* user_data);
void DestroyAudioOut(AudioOut* audio_out);

std::uint32_t GetAudioOutSampleRate(const AudioOut* audio_out);
std::uint32_t GetAudioOutChannelCount(const AudioOut* audio_out);

} // namespace AudioCore::Horizon
