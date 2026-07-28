// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>

#include "citra_switch/canvas.h"

namespace SwitchFrontend {

bool Font::Init() {
    if (initialised) {
        return valid;
    }
    initialised = true;
    if (R_FAILED(plInitialize(PlServiceType_User))) {
        return false;
    }
    if (FT_Init_FreeType(&library) != 0) {
        return false;
    }
    AddSharedFace(PlSharedFontType_Standard);
    AddSharedFace(PlSharedFontType_ChineseSimplified);
    AddSharedFace(PlSharedFontType_ExtChineseSimplified);
    AddSharedFace(PlSharedFontType_KO);
    valid = !faces.empty();
    return valid;
}

void Font::Shutdown() {
    if (!initialised) {
        return;
    }
    cache.clear();
    for (FT_Face face : faces) {
        FT_Done_Face(face);
    }
    faces.clear();
    if (library) {
        FT_Done_FreeType(library);
        library = nullptr;
    }
    plExit();
    initialised = false;
    valid = false;
}

int Font::Draw(Canvas& canvas, int x, int baseline, std::string_view text, int size, u32 color) {
    int pen = x;
    std::size_t i = 0;
    while (i < text.size()) {
        const u32 cp = DecodeUtf8(text, i);
        const Glyph* g = GetGlyph(cp, size);
        if (!g) {
            continue;
        }
        const int gx = pen + g->left;
        const int gy = baseline - g->top;
        for (int row = 0; row < g->h; ++row) {
            for (int col = 0; col < g->w; ++col) {
                canvas.Blend(gx + col, gy + row, color, g->coverage[row * g->w + col]);
            }
        }
        pen += g->advance;
    }
    return pen - x;
}

int Font::Measure(std::string_view text, int size) {
    int w = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const u32 cp = DecodeUtf8(text, i);
        if (const Glyph* g = GetGlyph(cp, size)) {
            w += g->advance;
        }
    }
    return w;
}

std::string Font::Truncate(std::string_view text, int size, int maxw) {
    if (Measure(text, size) <= maxw) {
        return std::string{text};
    }
    const int ell = Measure("…", size);
    std::string out;
    int w = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        const u32 cp = DecodeUtf8(text, i);
        const Glyph* g = GetGlyph(cp, size);
        const int adv = g ? g->advance : 0;
        if (w + adv + ell > maxw) {
            break;
        }
        out.append(text.substr(start, i - start));
        w += adv;
    }
    out.append("…");
    return out;
}

std::string Font::TruncateFront(std::string_view text, int size, int maxw) {
    if (Measure(text, size) <= maxw) {
        return std::string{text};
    }
    const int ell = Measure("…", size);
    std::size_t i = 0;
    while (i < text.size()) {
        DecodeUtf8(text, i);
        const std::string_view tail = text.substr(i);
        if (ell + Measure(tail, size) <= maxw) {
            return "…" + std::string{tail};
        }
    }
    return "…";
}

void Font::AddSharedFace(PlSharedFontType type) {
    PlFontData data{};
    if (R_FAILED(plGetSharedFontByType(&data, type))) {
        return;
    }
    FT_Face face{};
    if (FT_New_Memory_Face(library, static_cast<const FT_Byte*>(data.address),
                           static_cast<FT_Long>(data.size), 0, &face) == 0) {
        faces.push_back(face);
    }
}

const Font::Glyph* Font::GetGlyph(u32 cp, int size) {
    const u64 key = (static_cast<u64>(size) << 32) | cp;
    if (auto it = cache.find(key); it != cache.end()) {
        return &it->second;
    }
    FT_Face face = faces.empty() ? nullptr : faces.front();
    for (FT_Face candidate : faces) {
        if (FT_Get_Char_Index(candidate, cp) != 0) {
            face = candidate;
            break;
        }
    }
    if (!face) {
        return nullptr;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size));
    // NO_AUTOHINT keeps rendering on the font's native TrueType hinter.
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_NO_AUTOHINT) != 0) {
        return nullptr;
    }
    const FT_GlyphSlot slot = face->glyph;
    Glyph g;
    g.w = static_cast<int>(slot->bitmap.width);
    g.h = static_cast<int>(slot->bitmap.rows);
    g.left = slot->bitmap_left;
    g.top = slot->bitmap_top;
    g.advance = static_cast<int>(slot->advance.x >> 6);
    g.coverage.resize(static_cast<std::size_t>(g.w) * g.h);
    for (int row = 0; row < g.h; ++row) {
        std::memcpy(g.coverage.data() + row * g.w, slot->bitmap.buffer + row * slot->bitmap.pitch,
                    g.w);
    }
    return &cache.emplace(key, std::move(g)).first->second;
}

u32 Font::DecodeUtf8(std::string_view s, std::size_t& i) {
    const u8 c = static_cast<u8>(s[i++]);
    if (c < 0x80) {
        return c;
    }
    int extra = 0;
    u32 cp = 0;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07;
    } else {
        return '?';
    }
    for (int k = 0; k < extra && i < s.size(); ++k) {
        cp = (cp << 6) | (static_cast<u8>(s[i++]) & 0x3F);
    }
    return cp;
}

Font& GetSharedFont() {
    static Font font;
    return font;
}

int CenterBaseline(int y, int h, int size) {
    return y + (h + static_cast<int>(size * 0.7f)) / 2;
}

void DrawListScrollbar(Canvas& c, int track_x, int top, int visible_rows, int row_h, int count,
                       int scroll) {
    if (count <= visible_rows) {
        return;
    }
    const int track_h = visible_rows * row_h;
    c.FillRoundRect(track_x, top, 4, track_h, 2, kColRail);
    const int thumb_h = std::max(24, track_h * visible_rows / count);
    const int max_scroll = count - visible_rows;
    const int thumb_y = top + (track_h - thumb_h) * scroll / std::max(1, max_scroll);
    c.FillRoundRect(track_x, thumb_y, 4, thumb_h, 2, kColAccent);
}

int DrawButtonChip(Canvas& canvas, int x, int y, const char* button) {
    constexpr int chip_h = 26;
    Font& font = GetSharedFont();
    const int letter_w = font.Measure(button, 18);
    const int chip_w = std::max(chip_h, letter_w + 16);
    canvas.FillRoundRect(x, y, chip_w, chip_h, chip_h / 2, kColBadge);
    font.Draw(canvas, x + (chip_w - letter_w) / 2, CenterBaseline(y, chip_h, 18), button, 18,
              kColText);
    return chip_w;
}

int DrawHint(Canvas& canvas, int x, int y, const char* button, const char* label) {
    constexpr int chip_h = 26;
    const int chip_w = DrawButtonChip(canvas, x, y, button);
    const int label_x = x + chip_w + 8;
    const int label_w = GetSharedFont().Draw(canvas, label_x, CenterBaseline(y, chip_h, 18), label,
                                             18, kColTextDim);
    return chip_w + 8 + label_w;
}

void DrawFlavorSentence(Canvas& canvas, int x, int baseline_y, u32 text_color,
                        std::initializer_list<FlavorPart> parts) {
    constexpr int chip_h = 26;
    constexpr int font_size = 18;
    // Inverse of CenterBaseline(top, h, size): the chip top that makes its own text land on
    // baseline_y, same as the plain-text parts drawn directly at baseline_y.
    const int chip_top = baseline_y - (chip_h + static_cast<int>(font_size * 0.7f)) / 2;
    int cx = x;
    for (const FlavorPart& part : parts) {
        if (part.chip != nullptr) {
            cx += DrawButtonChip(canvas, cx, chip_top, part.chip) + 6;
        } else {
            cx += GetSharedFont().Draw(canvas, cx, baseline_y, part.text, font_size, text_color);
        }
    }
}

void DrawConfirmDialog(Canvas& c, const char* title, const char* line1, const char* line2,
                       const char* confirm_label, const char* cancel_label) {
    constexpr int w = 620;
    constexpr int h = 200;
    const int x = (kScreenW - w) / 2;
    const int y = (kScreenH - h) / 2;
    Font& font = GetSharedFont();

    c.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xC0));
    c.RoundBorder(x, y, w, h, 14, 2, kColBadge, kColSurface);

    int ty = y + 20;
    font.Draw(c, x + 24, ty + 22, title, 24, kColText);
    ty += 40;
    if (line1 != nullptr && line1[0] != '\0') {
        font.Draw(c, x + 24, ty + 18, line1, 18, kColTextDim);
        ty += 24;
    }
    if (line2 != nullptr && line2[0] != '\0') {
        font.Draw(c, x + 24, ty + 18, line2, 18, kColTextDim);
    }

    int hx = x + 24;
    const int hy = y + h - 38;
    hx += DrawHint(c, hx, hy, "A", confirm_label) + 22;
    DrawHint(c, hx, hy, "B", cancel_label);
}

} // namespace SwitchFrontend
