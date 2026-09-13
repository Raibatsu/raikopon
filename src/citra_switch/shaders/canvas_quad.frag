// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450

layout(binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) in float in_mode;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 texel = texture(atlas, in_uv);
    if (in_mode > 0.5) {
        out_color = vec4(texel.rgb, in_color.a * texel.a);
    } else {
        out_color = vec4(in_color.rgb, in_color.a * texel.r);
    }
}
