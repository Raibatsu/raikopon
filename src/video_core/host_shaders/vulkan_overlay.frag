// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec4 color;

layout (push_constant) uniform PushConstants {
    vec4 overlay_color;
} pc;

void main() {
    color = pc.overlay_color;
}
