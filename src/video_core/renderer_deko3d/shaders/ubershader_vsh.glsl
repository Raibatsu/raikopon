// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

// Trivial vertex shader for the deko3d ubershader pipeline.

// HardwareVertex
layout (location = 0) in vec4 vert_position;
layout (location = 1) in vec4 vert_color;
layout (location = 2) in vec2 vert_texcoord0;
layout (location = 3) in vec2 vert_texcoord1;
layout (location = 4) in vec2 vert_texcoord2;
layout (location = 5) in float vert_texcoord0_w;
layout (location = 6) in vec4 vert_normquat;
layout (location = 7) in vec3 vert_view;

// Interface to the fragment ubershader.
layout (location = 1) out vec4 primary_color;
layout (location = 2) out vec2 texcoord0;
layout (location = 3) out vec2 texcoord1;
layout (location = 4) out vec2 texcoord2;
layout (location = 5) out float texcoord0_w;
layout (location = 6) out vec4 normquat;
layout (location = 7) out vec3 view;

layout (binding = 1, std140) uniform vs_data {
    bool enable_clip1;
    bool flip_viewport;
    vec4 clip_coef;
};

out gl_PerVertex {
    vec4 gl_Position;
    float gl_ClipDistance[2];
};

const vec2 EPSILON_Z = vec2(0.000001f, -1.00001f);

vec4 SanitizeVertex(vec4 vtx_pos) {
    float ndc_z = vtx_pos.z / vtx_pos.w;
    if (ndc_z > 0.f && ndc_z < EPSILON_Z[0]) {
        vtx_pos.z = 0.f;
    }
    if (ndc_z < -1.f && ndc_z > EPSILON_Z[1]) {
        vtx_pos.z = -vtx_pos.w;
    }
    return vtx_pos;
}

void main() {
    primary_color = vert_color;
    texcoord0 = vert_texcoord0;
    texcoord1 = vert_texcoord1;
    texcoord2 = vert_texcoord2;
    texcoord0_w = vert_texcoord0_w;
    normquat = vert_normquat;
    view = vert_view;

    vec4 vtx_pos = SanitizeVertex(vert_position);
    if (flip_viewport) {
        vtx_pos.y = -vtx_pos.y;
    }
    gl_Position = vec4(vtx_pos.x, vtx_pos.y, -vtx_pos.z, vtx_pos.w);

    // PICA's fixed clipping plane z <= 0
    gl_ClipDistance[0] = -vtx_pos.z;
    if (enable_clip1) {
        gl_ClipDistance[1] = dot(clip_coef, vtx_pos);
    } else {
        gl_ClipDistance[1] = 0.0;
    }
}
