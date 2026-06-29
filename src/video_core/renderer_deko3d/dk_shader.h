// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include "common/common_types.h"
#include "common/vector_math.h"

namespace Pica::Shader {
struct FSConfig;
} // namespace Pica::Shader

namespace Deko3D {

/**
 * CPU-side mirror of the `fs_config` std140 uniform block in renderer_deko3d/shaders/ubershader_fsh.glsl.
 */
struct FSConfigUniformData {
    u32 alpha_test_func;      // FramebufferRegs::CompareFunc
    u32 scissor_mode;         // RasterizerRegs::ScissorMode
    u32 depthmap_enable;      // 0 = Z-buffering, 1 = W-buffering
    u32 texture0_type;        // TexturingRegs::TextureConfig::TextureType
    u32 texture2_use_coord1;
    u32 combiner_buffer_mask; // tev_combiner_buffer_input (8 bits)
    u32 fog_mode;             // TexturingRegs::FogMode
    u32 fog_flip;
    u32 lighting;             // LightConfig.raw
    u32 proctex_enable;
    u32 tex_border;           // 3 units x (enable_s | enable_t << 1)
    u32 pad0;
    // x = sources_raw, y = modifiers_raw, z = ops_raw, w = scales_raw
    alignas(16) std::array<Common::Vec4u, 6> tev;
    // 8 packed Light.raw words: light[i >> 2][i & 3]
    alignas(16) std::array<Common::Vec4u, 2> light;
    // 7 packed LutConfig.raw words (d0, d1, sp, fr, rr, rg, rb)
    alignas(16) std::array<Common::Vec4u, 2> lut_raw;
    // Matching 7 LUT scales
    alignas(16) std::array<Common::Vec4f, 2> lut_scale;
};

static_assert(sizeof(FSConfigUniformData) == 240,
              "FSConfigUniformData does not match the fs_config block in ubershader_fsh.glsl");

/// Packs a PICA fragment configuration into the fs_config block.
[[nodiscard]] FSConfigUniformData BuildFSConfigUniform(const Pica::Shader::FSConfig& config);

} // namespace Deko3D
