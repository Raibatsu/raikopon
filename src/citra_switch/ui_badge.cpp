// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citra_switch/ui_badge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace SwitchFrontend {

namespace {

constexpr float kBadgeOutline = 2.0f;
constexpr float kTextScale = 0.7f;
constexpr std::uint32_t kCircleMaskSize = 64;

constexpr CanvasColor kOutlineColor{0.08f, 0.08f, 0.09f, 1.0f};
constexpr CanvasColor kColorA{0.29f, 0.69f, 0.93f, 1.0f};
constexpr CanvasColor kColorB{0.96f, 0.79f, 0.12f, 1.0f};
constexpr CanvasColor kColorX{0.14f, 0.68f, 0.43f, 1.0f};
constexpr CanvasColor kColorY{0.88f, 0.28f, 0.36f, 1.0f};
constexpr CanvasColor kColorNeutral{0.32f, 0.32f, 0.34f, 1.0f};
constexpr CanvasColor kSymbolColor{1.0f, 1.0f, 1.0f, 1.0f};

AtlasRegion s_circle_mask{};

} // namespace

void LoadBadgeAssets(GpuCanvas& canvas, GlyphAtlas& atlas) {
    s_circle_mask = atlas.ReserveAtlasRegion(canvas, kCircleMaskSize, kCircleMaskSize);
    if (s_circle_mask.u1 <= s_circle_mask.u0) {
        return;
    }

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kCircleMaskSize) * kCircleMaskSize *
                                   4);
    const float center = static_cast<float>(kCircleMaskSize - 1) * 0.5f;
    const float radius = static_cast<float>(kCircleMaskSize) * 0.5f - 1.0f;
    for (std::uint32_t y = 0; y < kCircleMaskSize; ++y) {
        for (std::uint32_t x = 0; x < kCircleMaskSize; ++x) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float coverage = std::clamp(radius - dist + 0.5f, 0.0f, 1.0f);
            const std::uint8_t v = static_cast<std::uint8_t>(coverage * 255.0f);
            std::uint8_t* texel =
                rgba.data() + (static_cast<std::size_t>(y) * kCircleMaskSize + x) * 4;
            texel[0] = texel[1] = texel[2] = texel[3] = v;
        }
    }
    canvas.UpdateAtlasRegion(s_circle_mask.x, s_circle_mask.y, kCircleMaskSize, kCircleMaskSize,
                             rgba.data());
}

CanvasColor ButtonBadgeColor(const char* label) {
    if (std::strcmp(label, "A") == 0) {
        return kColorA;
    }
    if (std::strcmp(label, "B") == 0) {
        return kColorB;
    }
    if (std::strcmp(label, "X") == 0) {
        return kColorX;
    }
    if (std::strcmp(label, "Y") == 0) {
        return kColorY;
    }
    return kColorNeutral;
}

void DrawButtonBadge(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float center_y, float size,
                     const char* label) {
    if (s_circle_mask.u1 <= s_circle_mask.u0) {
        return;
    }

    const float outer = size + kBadgeOutline * 2.0f;
    canvas.DrawTexturedQuad(x - kBadgeOutline, center_y - outer * 0.5f, outer, outer,
                            s_circle_mask.u0, s_circle_mask.v0, s_circle_mask.u1, s_circle_mask.v1,
                            kOutlineColor);
    canvas.DrawTexturedQuad(x, center_y - size * 0.5f, size, size, s_circle_mask.u0,
                            s_circle_mask.v0, s_circle_mask.u1, s_circle_mask.v1,
                            ButtonBadgeColor(label));

    const float letter_width = MeasureText(atlas, canvas, label, kTextScale);
    DrawText(canvas, atlas, x + (size - letter_width) * 0.5f,
            center_y - atlas.LineHeight() * kTextScale * 0.5f, label, kSymbolColor, kTextScale);
}

} // namespace SwitchFrontend
