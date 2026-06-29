// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

// Samples one 3DS framebuffer texture across a quad.

layout (location = 0) in vec2 vTexCoord;
layout (location = 0) out vec4 oColor;

layout (binding = 0) uniform sampler2D uTexture;

void main() {
    oColor = texture(uTexture, vTexCoord);
}
