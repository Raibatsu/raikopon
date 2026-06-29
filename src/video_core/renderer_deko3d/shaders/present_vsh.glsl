// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#version 460

// Full-screen-quad present vertex shader.

layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aTexCoord;

layout (location = 0) out vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
