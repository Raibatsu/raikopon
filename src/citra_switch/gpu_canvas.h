// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <memory>

namespace SwitchFrontend {

struct CanvasColor {
    float r;
    float g;
    float b;
    float a;
};

inline constexpr std::uint32_t kCaptureSlotCount = 2;

class GpuCanvas {
public:
    GpuCanvas();
    ~GpuCanvas();

    GpuCanvas(const GpuCanvas&) = delete;
    GpuCanvas& operator=(const GpuCanvas&) = delete;

    bool Init();
    void Shutdown();
    bool IsValid() const;

    bool BeginFrame(CanvasColor clear_color);
    void DrawQuad(float x, float y, float w, float h, CanvasColor color,
                  float rotation_radians = 0.0f);
    // full_color = true samples the atlas's own RGBA texel directly (real box-art icons) instead
    // of treating it as a coverage mask tinted by `tint` (the default, used by text/UI icons).
    void DrawTexturedQuad(float x, float y, float w, float h, float u0, float v0, float u1,
                          float v1, CanvasColor tint, float rotation_radians = 0.0f,
                          bool full_color = false);
    void SetClipRect(float x, float y, float w, float h);
    void ClearClipRect();
    void EndFrame();

    bool BeginCapture(std::uint32_t slot, CanvasColor clear_color);
    void EndCapture();
    void DrawCaptureQuad(std::uint32_t slot, float x, float y, float w, float h,
                         float rotation_radians = 0.0f, float src_x = 0.0f, float src_y = 0.0f,
                         float src_w = -1.0f, float src_h = -1.0f);

    // pixels is RGBA8 (4 bytes/texel, width*height*4 total) regardless of full_color use - even
    // coverage-mask callers (glyphs, UI icons) must replicate their single channel into R=G=B=A.
    void UpdateAtlasRegion(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                           std::uint32_t height, const std::uint8_t* pixels);
    std::uint32_t AtlasWidth() const;
    std::uint32_t AtlasHeight() const;

    std::uint32_t Width() const;
    std::uint32_t Height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace SwitchFrontend
