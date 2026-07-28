// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

// CPU-side pixel buffer, text, and drawing primitives shared by every native-framebuffer screen
// (the library menu in menu.cpp, and the in-game settings screen). Moved out of menu.cpp's
// anonymous namespace so a second translation unit can draw with the exact same code instead of
// cloning it.
namespace SwitchFrontend {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;

constexpr u32 MakeColor(u8 r, u8 g, u8 b, u8 a = 0xFF) {
    return (u32{a} << 24) | (u32{b} << 16) | (u32{g} << 8) | u32{r};
}

constexpr u32 kColBg = MakeColor(0x17, 0x18, 0x1B);
constexpr u32 kColRail = MakeColor(0x1E, 0x20, 0x24);
constexpr u32 kColSurface = MakeColor(0x24, 0x26, 0x2B);
constexpr u32 kColSurfaceHi = MakeColor(0x30, 0x33, 0x39);
constexpr u32 kColBadge = MakeColor(0x3A, 0x3C, 0x42);
constexpr u32 kColAccent = MakeColor(0xFA, 0xAA, 0x49);
constexpr u32 kColAccentDim = MakeColor(0x8C, 0x5F, 0x29);
constexpr u32 kColText = MakeColor(0xF1, 0xF2, 0xF4);
constexpr u32 kColTextDim = MakeColor(0x9B, 0xA0, 0xA6);
constexpr u32 kColOnAccent = MakeColor(0x17, 0x18, 0x1B);
constexpr u32 kColError = MakeColor(0xE0, 0x5A, 0x4A);
constexpr u32 kColHintBar = MakeColor(0x1B, 0x1C, 0x20);

class Canvas {
public:
    Canvas() : pixels(static_cast<std::size_t>(kScreenW) * kScreenH) {}

    u32* Data() {
        return pixels.data();
    }

    void Clear(u32 color) {
        std::fill(pixels.begin(), pixels.end(), color);
    }

    void Blend(int x, int y, u32 color, u8 coverage) {
        if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH || coverage == 0) {
            return;
        }
        const u32 a = ((color >> 24) & 0xFF) * coverage / 255;
        u32& dst = pixels[static_cast<std::size_t>(y) * kScreenW + x];
        if (a == 0) {
            return;
        }
        if (a >= 0xFF) {
            dst = (dst & 0xFF000000u) | (color & 0x00FFFFFFu);
            return;
        }
        const u32 inv = 255 - a;
        const u32 sr = color & 0xFF, sg = (color >> 8) & 0xFF, sb = (color >> 16) & 0xFF;
        const u32 dr = dst & 0xFF, dg = (dst >> 8) & 0xFF, db = (dst >> 16) & 0xFF;
        const u32 rr = (sr * a + dr * inv) / 255;
        const u32 rg = (sg * a + dg * inv) / 255;
        const u32 rb = (sb * a + db * inv) / 255;
        dst = MakeColor(static_cast<u8>(rr), static_cast<u8>(rg), static_cast<u8>(rb));
    }

    void FillRect(int x, int y, int w, int h, u32 color) {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(kScreenW, x + w), y1 = std::min(kScreenH, y + h);
        const u8 alpha = (color >> 24) & 0xFF;
        for (int yy = y0; yy < y1; ++yy) {
            if (alpha >= 0xFF) {
                std::fill_n(pixels.data() + static_cast<std::size_t>(yy) * kScreenW + x0, x1 - x0,
                            color);
            } else {
                for (int xx = x0; xx < x1; ++xx) {
                    Blend(xx, yy, color, alpha);
                }
            }
        }
    }

    void FillRoundRect(int x, int y, int w, int h, int r, u32 color) {
        r = std::clamp(r, 0, std::min(w, h) / 2);
        for (int row = 0; row < h; ++row) {
            int cut = 0;
            if (row < r) {
                const int t = r - 1 - row;
                cut = r - static_cast<int>(std::sqrt(static_cast<float>(r * r - t * t)));
            } else if (row >= h - r) {
                const int t = row - (h - r);
                cut = r - static_cast<int>(std::sqrt(static_cast<float>(r * r - t * t)));
            }
            FillRect(x + cut, y + row, w - 2 * cut, 1, color);
        }
    }

    void RoundBorder(int x, int y, int w, int h, int r, int thickness, u32 border, u32 inner) {
        FillRoundRect(x, y, w, h, r, border);
        FillRoundRect(x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness,
                      std::max(0, r - thickness), inner);
    }

    void BlitIcon(const std::vector<u32>& icon, int src_size, int dx, int dy, int dst_size) {
        if (icon.empty() || src_size <= 0) {
            return;
        }
        for (int oy = 0; oy < dst_size; ++oy) {
            const float sy = (oy + 0.5f) * src_size / dst_size - 0.5f;
            const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, src_size - 1);
            const int y1 = std::min(y0 + 1, src_size - 1);
            const float fy = std::clamp(sy - y0, 0.0f, 1.0f);
            for (int ox = 0; ox < dst_size; ++ox) {
                const float sx = (ox + 0.5f) * src_size / dst_size - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, src_size - 1);
                const int x1 = std::min(x0 + 1, src_size - 1);
                const float fx = std::clamp(sx - x0, 0.0f, 1.0f);
                const u32 c00 = icon[y0 * src_size + x0], c10 = icon[y0 * src_size + x1];
                const u32 c01 = icon[y1 * src_size + x0], c11 = icon[y1 * src_size + x1];
                float ch[3];
                for (int i = 0; i < 3; ++i) {
                    const int s = i * 8;
                    const float top = ((c00 >> s) & 0xFF) * (1 - fx) + ((c10 >> s) & 0xFF) * fx;
                    const float bot = ((c01 >> s) & 0xFF) * (1 - fx) + ((c11 >> s) & 0xFF) * fx;
                    ch[i] = top * (1 - fy) + bot * fy;
                }
                const int px = dx + ox, py = dy + oy;
                if (px >= 0 && py >= 0 && px < kScreenW && py < kScreenH) {
                    pixels[static_cast<std::size_t>(py) * kScreenW + px] = MakeColor(
                        static_cast<u8>(ch[0]), static_cast<u8>(ch[1]), static_cast<u8>(ch[2]));
                }
            }
        }
    }

private:
    std::vector<u32> pixels;
};

// FreeType-backed text rasterizer over the Switch's shared system font. Glyphs are cached per
// (size, codepoint) the first time they're drawn/measured.
class Font {
public:
    bool Init();
    void Shutdown();
    int Draw(Canvas& canvas, int x, int baseline, std::string_view text, int size, u32 color);
    int Measure(std::string_view text, int size);
    std::string Truncate(std::string_view text, int size, int maxw);
    std::string TruncateFront(std::string_view text, int size, int maxw);

private:
    struct Glyph {
        int w{}, h{}, left{}, top{}, advance{};
        std::vector<u8> coverage;
    };

    void AddSharedFace(PlSharedFontType type);
    const Glyph* GetGlyph(u32 cp, int size);
    static u32 DecodeUtf8(std::string_view s, std::size_t& i);

    bool initialised{};
    bool valid{};
    FT_Library library{};
    std::vector<FT_Face> faces;
    std::unordered_map<u64, Glyph> cache;
};

// One shared Font instance, alive for the whole process lifetime (Init() by RunMenu() at
// startup, Shutdown() by ShutdownMenu() at exit) — safe to reuse from any native-framebuffer
// screen shown while a game is running, since ShutdownMenu() isn't called until the app exits.
Font& GetSharedFont();

int CenterBaseline(int y, int h, int size);

// Press-once-then-hold-to-repeat helper for D-pad/stick navigation. held_frames tracks how long
// each of the 4 directions has been continuously active.
struct Repeater {
    int held_frames[4]{};

    // Returns a bitmask of directions that should act this frame.
    u32 Step(bool up, bool down, bool left, bool right) {
        const bool active[4] = {up, down, left, right};
        u32 fired = 0;
        for (int d = 0; d < 4; ++d) {
            if (!active[d]) {
                held_frames[d] = 0;
                continue;
            }
            const int f = held_frames[d]++;
            if (f == 0 || (f >= 24 && (f - 24) % 5 == 0)) {
                fired |= 1u << d;
            }
        }
        return fired;
    }
};
enum { DirUp = 1, DirDown = 2, DirLeft = 4, DirRight = 8 };

void DrawListScrollbar(Canvas& c, int track_x, int top, int visible_rows, int row_h, int count,
                       int scroll);

// The rounded pill by itself, with no trailing label — reused by DrawHint below and by any
// sentence that wants a button badge inline with prose (see DrawFlavorSentence).
int DrawButtonChip(Canvas& canvas, int x, int y, const char* button);

// Draws a small button chip followed by its text label.
int DrawHint(Canvas& canvas, int x, int y, const char* button, const char* label);

// One piece of a chip-and-prose sentence: either plain text, or a button badge (when `chip` is
// non-null, `text` is ignored).
struct FlavorPart {
    const char* text = nullptr;
    const char* chip = nullptr;
};

// Draws a sequence of text/chip parts left-to-right at a shared baseline, e.g. {"Press "},
// {.chip="A"}, {" to toggle."} — button names read as the same rounded badges the hint bar uses,
// rather than plain words. `baseline_y` is a text baseline, not a chip top.
void DrawFlavorSentence(Canvas& canvas, int x, int baseline_y, u32 text_color,
                        std::initializer_list<FlavorPart> parts);

// A centered dim-scrim confirm dialog: title, up to two body lines, and A/B hints at the bottom.
// Shared by the library (menu.cpp) and the in-game settings screen so every "are you sure?"
// prompt looks the same. Purely a draw call — the caller owns the open/confirm/cancel state and
// decides what A/B do.
void DrawConfirmDialog(Canvas& c, const char* title, const char* line1, const char* line2,
                       const char* confirm_label, const char* cancel_label);

} // namespace SwitchFrontend
