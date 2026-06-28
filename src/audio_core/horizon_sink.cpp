// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/horizon_sink.h"

#include <array>
#include <utility>
#include "audio_core/audio_types.h"
#include "audio_core/horizon_audio_out.h"
#include "common/logging/log.h"

// Read and play audio and convert 3DS 32Khz audio to libnx 48Khz audio
namespace AudioCore {

namespace {

constexpr std::size_t SourceBatchFrames = 1024;

} // namespace

struct HorizonSink::Impl {
    Horizon::AudioOut* audio_out{};
    std::function<void(s16*, std::size_t)> callback;
    std::array<s16, SourceBatchFrames * 2> source{};
    std::array<s32, 2> current{};
    std::array<s32, 2> next{};
    std::size_t source_offset = SourceBatchFrames;
    u32 phase{};
    u32 output_rate{};
    bool resampler_ready{};
    bool started{};

    std::array<s32, 2> ReadSourceFrame() {
        if (source_offset == SourceBatchFrames) {
            callback(source.data(), SourceBatchFrames);
            source_offset = 0;
        }
        const std::array<s32, 2> frame{source[source_offset * 2], source[source_offset * 2 + 1]};
        ++source_offset;
        return frame;
    }

    void Fill(s16* output, std::size_t frames) {
        if (!resampler_ready) {
            current = ReadSourceFrame();
            next = ReadSourceFrame();
            resampler_ready = true;
        }

        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const s64 delta = static_cast<s64>(next[channel]) - current[channel];
                output[frame * 2 + channel] = static_cast<s16>(
                    current[channel] + delta * phase / static_cast<s64>(output_rate));
            }

            phase += native_sample_rate;
            while (phase >= output_rate) {
                phase -= output_rate;
                current = next;
                next = ReadSourceFrame();
            }
        }
    }

    static void FillCallback(void* user_data, std::int16_t* output, std::size_t frames) {
        static_cast<Impl*>(user_data)->Fill(output, frames);
    }
};

HorizonSink::HorizonSink(std::string_view) : impl(std::make_unique<Impl>()) {
    u32 result = 0;
    impl->audio_out = Horizon::CreateAudioOut(result);
    if (impl->audio_out == nullptr) {
        LOG_CRITICAL(Audio_Sink, "audoutInitialize failed: 0x{:08X}", result);
        return;
    }

    impl->output_rate = Horizon::GetAudioOutSampleRate(impl->audio_out);
    const u32 channels = Horizon::GetAudioOutChannelCount(impl->audio_out);
    if (impl->output_rate == 0 || channels != 2) {
        LOG_CRITICAL(Audio_Sink, "Unsupported audout format: {} Hz, {} channels", impl->output_rate,
                     channels);
        Horizon::DestroyAudioOut(std::exchange(impl->audio_out, nullptr));
        return;
    }
    LOG_INFO(Audio_Sink, "Initialized Switch audout: {} Hz, {} channels",
             impl->output_rate, channels);
}

HorizonSink::~HorizonSink() {
    Horizon::DestroyAudioOut(impl->audio_out);
}

unsigned int HorizonSink::GetNativeSampleRate() const {
    return native_sample_rate;
}

void HorizonSink::SetCallback(std::function<void(s16*, std::size_t)> cb) {
    impl->callback = std::move(cb);
    if (impl->audio_out == nullptr || impl->started) {
        return;
    }

    const u32 result = Horizon::StartAudioOut(impl->audio_out, &Impl::FillCallback, impl.get());
    if (result != 0) {
        LOG_CRITICAL(Audio_Sink, "audoutStartAudioOut failed: 0x{:08X}", result);
        return;
    }
    impl->started = true;
}

std::vector<std::string> ListHorizonSinkDevices() {
    return {"Default"};
}

} // namespace AudioCore
