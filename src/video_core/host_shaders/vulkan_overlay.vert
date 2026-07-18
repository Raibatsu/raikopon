// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 vert_position;
layout (location = 1) in vec2 vert_texcoord;

layout (location = 0) out vec2 frag_texcoord;

void main() {
    frag_texcoord = vert_texcoord;
    gl_Position = vec4(vert_position, 0.0, 1.0);
}
