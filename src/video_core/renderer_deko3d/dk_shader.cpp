// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/pica/regs_rasterizer.h"
#include "video_core/renderer_deko3d/dk_shader.h"
#include "video_core/shader/generator/pica_fs_config.h"

namespace Deko3D {

FSConfigUniformData BuildFSConfigUniform(const Pica::Shader::FSConfig& config) {
    const auto& fb = config.framebuffer;
    const auto& tex = config.texture;
    const auto& lighting = config.lighting;

    FSConfigUniformData data{};
    data.alpha_test_func = static_cast<u32>(fb.alpha_test_func.Value());
    data.scissor_mode = static_cast<u32>(fb.scissor_test_mode.Value());
    data.depthmap_enable =
        fb.depthmap_enable == Pica::RasterizerRegs::DepthBuffering::WBuffering ? 1u : 0u;
    data.texture0_type = static_cast<u32>(tex.texture0_type.Value());
    data.texture2_use_coord1 = tex.texture2_use_coord1.Value();
    data.combiner_buffer_mask = tex.combiner_buffer_input.Value();
    data.fog_mode = static_cast<u32>(tex.fog_mode.Value());
    data.fog_flip = tex.fog_flip.Value();
    data.lighting = lighting.raw;
    data.proctex_enable = config.proctex.enable.Value();

    // (unit*2) = enable_s, bit (unit*2+1) = enable_t.
    data.tex_border = 0;
    for (u32 unit = 0; unit < 3; ++unit) {
        if (tex.texture_border_color[unit].enable_s) {
            data.tex_border |= 1u << (unit * 2);
        }
        if (tex.texture_border_color[unit].enable_t) {
            data.tex_border |= 1u << (unit * 2 + 1);
        }
    }

    // Pass the raw register words
    for (u32 i = 0; i < tex.tev_stages.size(); ++i) {
        const auto& stage = tex.tev_stages[i];
        data.tev[i] = {stage.sources_raw, stage.modifiers_raw, stage.ops_raw, stage.scales_raw};
    }

    // 8 lights packed four-per-vec4.
    for (u32 i = 0; i < lighting.lights.size(); ++i) {
        data.light[i >> 2][i & 3] = lighting.lights[i].raw;
    }

    // 7 lighting LUTs (d0, d1, sp, fr, rr, rg, rb), raw config + scale.
    const std::array luts = {&lighting.lut_d0, &lighting.lut_d1, &lighting.lut_sp, &lighting.lut_fr,
                             &lighting.lut_rr, &lighting.lut_rg, &lighting.lut_rb};
    for (u32 i = 0; i < luts.size(); ++i) {
        data.lut_raw[i >> 2][i & 3] = luts[i]->raw;
        data.lut_scale[i >> 2][i & 3] = luts[i]->GetScale();
    }

    return data;
}

} // namespace Deko3D
