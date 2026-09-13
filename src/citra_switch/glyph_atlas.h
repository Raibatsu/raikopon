// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "citra_switch/gpu_canvas.h"

namespace SwitchFrontend {

struct GlyphMetrics {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float bearing_x = 0.0f;
    float bearing_y = 0.0f;
    float advance = 0.0f;
};

struct AtlasRegion {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

class GlyphAtlas {
public:
    GlyphAtlas();
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    bool Init(GpuCanvas& canvas, float pixel_height);
    void Shutdown();

    // Loads the Switch OS's own shared system font(s) for `cfg_language` (a
    // Service::CFG::SystemLanguage value, 0-11 - identical ordering to libnx's SetLanguage, see
    // settings_model.cpp's LanguageName) as fallback faces, on top of the bundled Latin font -
    // needed for Japanese/Korean/Chinese, and improves diacritic/Cyrillic coverage for the rest.
    // No-op if `cfg_language` is already loaded. Already-rasterized glyphs stay valid across a
    // call to this (the glyph cache is keyed by codepoint only, not by which face supplied it) -
    // this only ever adds coverage for glyphs not seen yet, never invalidates anything.
    void SetLanguage(int cfg_language);

    const GlyphMetrics* GetGlyph(GpuCanvas& canvas, char32_t codepoint);
    float LineHeight() const;
    float Ascender() const;

    // Claims a width x height block of the shared atlas for a caller-owned upload (icons, etc.),
    // using the same shelf packer GetGlyph() does, so the two can never collide. The caller still
    // does the actual pixel upload via canvas.UpdateAtlasRegion() at the returned x/y.
    AtlasRegion ReserveAtlasRegion(GpuCanvas& canvas, std::uint32_t width, std::uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// scale multiplies every glyph's drawn size and advance (not re-rasterized - the shared atlas only
// bakes one pixel size, so a scale below 1.0 just draws its glyphs smaller, which is a safe minify
// since GpuCanvas's sampler is bilinear). Callers scaling text down should also scale
// Ascender()/LineHeight() themselves when computing y - see ui_badge.cpp's DrawButtonBadge.
float MeasureText(GlyphAtlas& atlas, GpuCanvas& canvas, std::string_view utf8, float scale = 1.0f);
float DrawText(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float y, std::string_view utf8,
              CanvasColor color, float scale = 1.0f);

// Greedy word wrap, honoring explicit newlines already in the source text (paragraph breaks). A
// single word wider than max_width is placed on its own line rather than dropped or split, so it
// can't produce an infinite loop.
std::vector<std::string> WrapText(GpuCanvas& canvas, GlyphAtlas& atlas, std::string_view text,
                                  float max_width);

} // namespace SwitchFrontend
