// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 frag_texcoord;

layout (location = 0) out vec4 color;

// Bound per-batch - the font/glyph atlas for text and flat rects, or a per-boot game icon for
// the loading screen's background (see RendererVulkan::CreateLoadingIcon).
layout (set = 0, binding = 0) uniform sampler2D overlay_texture;

layout (push_constant) uniform PushConstants {
    vec4 overlay_color;
    // 0: overlay_texture holds glyph/white-pixel coverage in R, multiplied by overlay_color
    //    (text, flat rects - the original and still most common case).
    // 1: overlay_texture is sampled directly as color, tinted by overlay_color (the loading
    //    screen's icon background - lets one draw call both tint and dull it).
    int mode;
} pc;

void main() {
    if (pc.mode == 1) {
        vec4 texel = texture(overlay_texture, frag_texcoord);
        color = vec4(texel.rgb * pc.overlay_color.rgb, texel.a * pc.overlay_color.a);
    } else {
        float coverage = texture(overlay_texture, frag_texcoord).r;
        color = vec4(pc.overlay_color.rgb, pc.overlay_color.a * coverage);
    }
}
