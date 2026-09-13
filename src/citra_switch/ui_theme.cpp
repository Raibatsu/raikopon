// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_theme.h"

namespace SwitchFrontend {

namespace {

constexpr CanvasColor FromRgb8(int r, int g, int b, float a = 1.0f) {
    return CanvasColor{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                       static_cast<float>(b) / 255.0f, a};
}

} // namespace

const UiPalette& DarkPalette() {
    static const UiPalette palette{
        .bg = FromRgb8(0x17, 0x18, 0x1B),
        .surface = FromRgb8(0x24, 0x26, 0x2B),
        .surface_warm = FromRgb8(0x39, 0x36, 0x2D),
        .accent = FromRgb8(0xFA, 0xAA, 0x49),
        .accent_dim = FromRgb8(0x8C, 0x5F, 0x29),
        .text = FromRgb8(0xF1, 0xF2, 0xF4),
        .text_dim = FromRgb8(0x9B, 0xA0, 0xA6),
    };
    return palette;
}

const UiPalette& LightPalette() {
    static const UiPalette palette{
        .bg = FromRgb8(0xF4, 0xF1, 0xEA),
        .surface = FromRgb8(0xEA, 0xE6, 0xDD),
        .surface_warm = FromRgb8(0xF3, 0xDF, 0xC0),
        .accent = FromRgb8(0xC9, 0x7A, 0x1F),
        .accent_dim = FromRgb8(0x8C, 0x5F, 0x29),
        .text = FromRgb8(0x2A, 0x26, 0x20),
        .text_dim = FromRgb8(0x6B, 0x64, 0x59),
    };
    return palette;
}

const UiPalette& CurrentPalette() {
    return DarkPalette();
}

} // namespace SwitchFrontend
