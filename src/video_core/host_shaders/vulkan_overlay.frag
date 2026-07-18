// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 frag_texcoord;

layout (location = 0) out vec4 color;

layout (set = 0, binding = 0) uniform sampler2D font_atlas;

layout (push_constant) uniform PushConstants {
    vec4 overlay_color;
} pc;

void main() {
    // The atlas stores glyph coverage in R.
    float coverage = texture(font_atlas, frag_texcoord).r;
    color = vec4(pc.overlay_color.rgb, pc.overlay_color.a * coverage);
}
