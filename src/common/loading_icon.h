// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <vector>

// Hands the launched title's already-decoded icon from the frontend (which owns game scanning)
// to the Vulkan renderer's boot-time "Compiling shaders"/"Building pipelines" overlay (which has
// no way to look up a title's icon itself), without either side depending on the other's headers.
// Mirrors shader_compile_stats.h's global-state shape for the same reason: the producer and
// consumer run on different threads/modules and don't otherwise share a call path.
namespace Common::LoadingIcon {

struct Icon {
    // Square RGBA8 pixels (R in the lowest byte), size*size entries. Empty/size==0 means no icon
    // is available right now.
    std::vector<std::uint32_t> pixels;
    std::uint32_t size = 0;
};

// Copies `rgba_pixels` (size*size entries) so the caller's own buffer can be reused immediately.
void Set(const std::uint32_t* rgba_pixels, std::uint32_t size);

// Equivalent to Set(nullptr, 0) - for when the about-to-boot title has no usable icon.
void Clear();

// A copy of whatever was last Set(), or an empty Icon if nothing has been set (or it was cleared)
// since. Consumed once per boot by the renderer; safe to call from any thread.
Icon Get();

} // namespace Common::LoadingIcon
