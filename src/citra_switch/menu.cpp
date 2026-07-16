// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "citra_switch/menu.h"
#include "citra_switch/menu_data.h"

namespace SwitchFrontend {
namespace {

using u8 = std::uint8_t;
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

class Font {
public:
    bool Init() {
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

    void Shutdown() {
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

    int Draw(Canvas& canvas, int x, int baseline, std::string_view text, int size, u32 color) {
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

    int Measure(std::string_view text, int size) {
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

    std::string Truncate(std::string_view text, int size, int maxw) {
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

private:
    struct Glyph {
        int w{}, h{}, left{}, top{}, advance{};
        std::vector<u8> coverage;
    };

    void AddSharedFace(PlSharedFontType type) {
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

    const Glyph* GetGlyph(u32 cp, int size) {
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
            std::memcpy(g.coverage.data() + row * g.w,
                        slot->bitmap.buffer + row * slot->bitmap.pitch, g.w);
        }
        return &cache.emplace(key, std::move(g)).first->second;
    }

    static u32 DecodeUtf8(std::string_view s, std::size_t& i) {
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

    bool initialised{};
    bool valid{};
    FT_Library library{};
    std::vector<FT_Face> faces;
    std::unordered_map<u64, Glyph> cache;
};

Font g_font;

int CenterBaseline(int y, int h, int size) {
    return y + (h + static_cast<int>(size * 0.7f)) / 2;
}

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

enum class Tab { Library, Settings };

// Which pane the cursor lives in.
enum class Focus { Rail, Content };

constexpr int kRailW = 148;
constexpr int kHeaderH = 64;
constexpr int kHintH = 44;
constexpr int kContentX = kRailW;
constexpr int kContentTop = kHeaderH;
constexpr int kContentW = kScreenW - kRailW;
constexpr int kContentBottom = kScreenH - kHintH;

constexpr int kTileW = 196;
constexpr int kTileH = 170;
constexpr int kTileGap = 16;
constexpr int kIconSize = 96;

std::string g_notice;
int g_notice_frames = 0;

std::string ToLowerAscii(std::string_view s) {
    std::string out{s};
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

const char* RegionName(int region) {
    switch (region) {
    case -1:
        return "Auto";
    case 0:
        return "Japan";
    case 1:
        return "USA";
    case 2:
        return "Europe";
    case 3:
        return "Australia";
    case 4:
        return "China";
    case 5:
        return "Korea";
    case 6:
        return "Taiwan";
    default:
        return "Auto";
    }
}

const char* BackendName(int api) {
    switch (api) {
    case 0:
        return "Software";
    case 1:
        return "OpenGL";
    case 2:
        return "Vulkan";
    default:
        return "Unknown";
    }
}

std::string ResolutionText(int factor) {
    if (factor == 0) {
        return "Auto (window)";
    }
    if (factor == 1) {
        return "Native (1x)";
    }
    return std::to_string(factor) + "x";
}

struct SettingRow {
    const char* label;
    std::string value;
};

std::vector<SettingRow> BuildSettingRows(const MenuSettings& s) {
    return {
        {"Internal Resolution", ResolutionText(s.resolution_factor)},
        {"VSync", s.use_vsync ? "On" : "Off"},
        {"Async Shader Compilation", s.async_shader_compilation ? "On" : "Off"},
        {"Disk Shader Cache", s.use_disk_shader_cache ? "On" : "Off"},
        {"Show FPS Counter", s.show_fps ? "On" : "Off"},
        {"CPU Clock", std::to_string(s.cpu_clock_percentage) + "%"},
        {"New 3DS Mode", s.is_new_3ds ? "On" : "Off"},
        {"CPU JIT (dynarmic)", s.use_cpu_jit ? "On" : "Off"},
        {"Console Region", RegionName(s.region_value)},
    };
}
constexpr int kNumSettings = 9;

void CycleSetting(MenuSettings& s, int idx, int dir) {
    switch (idx) {
    case 0:
        s.resolution_factor = std::clamp(s.resolution_factor + dir, 0, 4);
        break;
    case 1:
        s.use_vsync = dir > 0;
        break;
    case 2:
        s.async_shader_compilation = dir > 0;
        break;
    case 3:
        s.use_disk_shader_cache = dir > 0;
        break;
    case 4:
        s.show_fps = dir > 0;
        break;
    case 5:
        s.cpu_clock_percentage = std::clamp(s.cpu_clock_percentage + dir * 25, 25, 400);
        break;
    case 6:
        s.is_new_3ds = dir > 0;
        break;
    case 7:
        s.use_cpu_jit = dir > 0;
        break;
    case 8:
        s.region_value = std::clamp(s.region_value + dir, -1, 6);
        break;
    default:
        break;
    }
}

// Draws a small button chip
int DrawHint(Canvas& canvas, int x, int y, const char* button, const char* label) {
    constexpr int chip_h = 26;
    const int letter_w = g_font.Measure(button, 18);
    const int chip_w = std::max(chip_h, letter_w + 16);
    canvas.FillRoundRect(x, y, chip_w, chip_h, chip_h / 2, kColBadge);
    g_font.Draw(canvas, x + (chip_w - letter_w) / 2, CenterBaseline(y, chip_h, 18), button, 18,
                kColText);
    const int label_x = x + chip_w + 8;
    const int label_w = g_font.Draw(canvas, label_x, CenterBaseline(y, chip_h, 18), label, 18,
                                    kColTextDim);
    return chip_w + 8 + label_w;
}

// Draws the two nav-rail icons.
// These look... fine? They aren't awful, but I'd love if someone actually made good icons.
void DrawRailIcon(Canvas& canvas, Tab tab, int cx, int cy, u32 color) {
    if (tab == Tab::Library) {
        // Four rounded tiles
        const int s = 12, g = 4;
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) {
                canvas.FillRoundRect(cx - s - g / 2 + c * (s + g), cy - s - g / 2 + r * (s + g), s,
                                     s, 3, color);
            }
        }
    } else {
        // Three sliders
        for (int r = 0; r < 3; ++r) {
            const int ly = cy - 10 + r * 10;
            canvas.FillRoundRect(cx - 14, ly, 28, 3, 1, color);
            canvas.FillRoundRect(cx - 14 + (r % 2 == 0 ? 16 : 4), ly - 3, 8, 9, 4, color);
        }
    }
}

void DrawRail(Canvas& canvas, Tab active, Tab cursor, bool rail_focused) {
    canvas.FillRect(0, 0, kRailW, kScreenH, kColRail);
    const std::array<std::pair<Tab, const char*>, 2> items{
        {{Tab::Library, "Library"}, {Tab::Settings, "Settings"}}};
    // The solid accent pill follows the cursor while the rail is focused.
    const Tab pill = rail_focused ? cursor : active;
    int y = 96;
    for (const auto& [tab, label] : items) {
        const bool on = tab == pill;
        const bool ghost = rail_focused && tab == active && tab != cursor;
        if (on) {
            canvas.FillRoundRect(12, y, kRailW - 24, 84, 16, kColAccent);
        } else if (ghost) {
            // Keep a faint marker on the section you came from.
            canvas.FillRoundRect(12, y, kRailW - 24, 84, 16, kColSurface);
        }
        const u32 fg = on ? kColOnAccent : kColTextDim;
        DrawRailIcon(canvas, tab, kRailW / 2, y + 32, fg);
        const int tw = g_font.Measure(label, 18);
        g_font.Draw(canvas, (kRailW - tw) / 2, y + 68, label, 18, fg);
        y += 108;
    }
}

void DrawHeader(Canvas& canvas, std::string_view subtitle) {
    canvas.FillRect(kContentX, 0, kContentW, kHeaderH, kColBg);
    g_font.Draw(canvas, kContentX + 24, CenterBaseline(0, kHeaderH, 28), "Dekopon", 28, kColText);
    if (!subtitle.empty()) {
        const int sw = g_font.Measure(subtitle, 20);
        g_font.Draw(canvas, kScreenW - 24 - sw, CenterBaseline(0, kHeaderH, 20), subtitle, 20,
                    kColTextDim);
    }
    canvas.FillRect(kContentX, kHeaderH - 1, kContentW, 1, kColRail);
}

void DrawNotice(Canvas& canvas) {
    if (g_notice_frames <= 0 || g_notice.empty()) {
        return;
    }
    const int pad = 20;
    const int tw = g_font.Measure(g_notice, 18);
    const int w = tw + pad * 2;
    const int x = kContentX + (kContentW - w) / 2;
    const int y = kContentBottom - 52;
    canvas.FillRoundRect(x, y, w, 36, 10, kColError);
    g_font.Draw(canvas, x + pad, CenterBaseline(y, 36, 18), g_notice, 18, kColText);
}

void DrawTile(Canvas& canvas, const GameEntry& game, int x, int y, bool selected,
              bool content_focused) {
    if (selected && content_focused) {
        canvas.RoundBorder(x, y, kTileW, kTileH, 14, 3, kColAccent, kColSurfaceHi);
    } else if (selected) {
        // Selected but the cursor is on the rail
        canvas.RoundBorder(x, y, kTileW, kTileH, 14, 3, kColBadge, kColSurfaceHi);
    } else {
        canvas.FillRoundRect(x, y, kTileW, kTileH, 14, kColSurface);
    }

    const int icon_x = x + (kTileW - kIconSize) / 2;
    const int icon_y = y + 14;
    if (!game.icon.empty()) {
        canvas.BlitIcon(game.icon, game.icon_size, icon_x, icon_y, kIconSize);
    } else {
        // Placeholder plate with the file type initial.
        canvas.FillRoundRect(icon_x, icon_y, kIconSize, kIconSize, 10, kColBadge);
        const std::string letter = game.file_type.empty() ? "?" : game.file_type.substr(0, 1);
        const int lw = g_font.Measure(letter, 40);
        g_font.Draw(canvas, icon_x + (kIconSize - lw) / 2, CenterBaseline(icon_y, kIconSize, 40),
                    letter, 40, kColTextDim);
    }

    const int text_w = kTileW - 24;
    const std::string title = g_font.Truncate(game.title, 18, text_w);
    const int title_w = g_font.Measure(title, 18);
    g_font.Draw(canvas, x + (kTileW - title_w) / 2, icon_y + kIconSize + 24, title, 18, kColText);

    // File type, plus a LOCKED marker for encrypted dumps.
    const int badge_y = y + kTileH - 30;
    if (!game.file_type.empty()) {
        const int bw = g_font.Measure(game.file_type, 14) + 14;
        canvas.FillRoundRect(x + 12, badge_y, bw, 20, 7, kColBadge);
        g_font.Draw(canvas, x + 12 + 7, CenterBaseline(badge_y, 20, 14), game.file_type, 14,
                    kColTextDim);
        if (game.encrypted) {
            const int lw = g_font.Measure("LOCKED", 14) + 14;
            canvas.FillRoundRect(x + 12 + bw + 6, badge_y, lw, 20, 7, kColAccentDim);
            g_font.Draw(canvas, x + 12 + bw + 6 + 7, CenterBaseline(badge_y, 20, 14), "LOCKED", 14,
                        kColText);
        }
    }
}

void DrawEmptyLibrary(Canvas& canvas) {
    const char* line1 = "No games found";
    const char* line2 = "Copy .3ds / .cci / .cxi / .3dsx files to";
    const char* line3 = "sdmc:/switch/dekopon/roms/";
    const int cx = kContentX + kContentW / 2;
    const int w1 = g_font.Measure(line1, 26);
    g_font.Draw(canvas, cx - w1 / 2, 300, line1, 26, kColText);
    const int w2 = g_font.Measure(line2, 18);
    g_font.Draw(canvas, cx - w2 / 2, 336, line2, 18, kColTextDim);
    const int w3 = g_font.Measure(line3, 18);
    g_font.Draw(canvas, cx - w3 / 2, 360, line3, 18, kColAccent);
}

// Layout of the library grid
struct Grid {
    int cols{};
    int start_x{};
    int top{};
    int visible_rows{};
};

Grid ComputeGrid() {
    Grid g;
    const int avail = kContentW - 48;
    g.cols = std::max(1, (avail + kTileGap) / (kTileW + kTileGap));
    const int used = g.cols * kTileW + (g.cols - 1) * kTileGap;
    g.start_x = kContentX + 24 + (avail - used) / 2;
    g.top = kContentTop + 20;
    g.visible_rows = std::max(1, (kContentBottom - g.top) / (kTileH + kTileGap));
    return g;
}

// swkbd search prompt
std::string PromptSearch(const std::string& initial) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return initial;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, "Search library");
    swkbdConfigSetGuideText(&kbd, "Game title");
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, 128);
    char out[256] = {};
    const Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc) ? std::string{out} : initial;
}

class Menu {
public:
    MenuResult Run(PadState& pad) {
        // Put a frame up before the ROM scan
        EnsureFramebuffer();
        DrawLoading();
        Present();
        Rescan();
        while (appletMainLoop()) {
            padUpdate(&pad);
            const u64 down = padGetButtonsDown(&pad);
            const u64 held = padGetButtons(&pad);

            // +/- together exits the app.
            if ((held & (HidNpadButton_Plus | HidNpadButton_Minus)) ==
                (HidNpadButton_Plus | HidNpadButton_Minus)) {
                FlushSettings();
                return {MenuAction::Exit, {}};
            }

            const HidAnalogStickState ls = padGetStickPos(&pad, 0);
            constexpr int dz = 12000;
            const u32 nav = repeater.Step(
                (down & HidNpadButton_Up) || ls.y > dz, (down & HidNpadButton_Down) || ls.y < -dz,
                (down & HidNpadButton_Left) || ls.x < -dz,
                (down & HidNpadButton_Right) || ls.x > dz);

            MenuResult result;
            bool done = false;
            if (focus == Focus::Rail) {
                HandleRail(down, nav);
            } else if (tab == Tab::Library) {
                done = HandleLibrary(down, nav, result);
            } else {
                done = HandleSettings(down, nav);
            }
            if (done) {
                return result;
            }

            HandleTouch();
            if (pending_launch) {
                MenuResult launch{MenuAction::Launch, *pending_launch};
                pending_launch.reset();
                return launch;
            }

            Draw();
            Present();
            if (g_notice_frames > 0) {
                --g_notice_frames;
            }
        }
        FlushSettings();
        return {MenuAction::Exit, {}};
    }

    // Releasing the framebuffer hands the nwindow back so the emulator's renderer can claim it for the launched game.
    ~Menu() {
        if (fb_ready) {
            framebufferClose(&fb);
        }
    }

private:
    Tab tab{Tab::Library};
    Focus focus{Focus::Content};
    Tab rail_sel{Tab::Library}; // Highlighted rail item while focused.
    std::vector<GameEntry> games;
    std::vector<int> filtered; // Indices into `games` after search filtering.
    int selected = 0;          // Index into `filtered`.
    int scroll_row = 0;
    int settings_sel = 0;
    std::string search;
    MenuSettings settings{};
    Repeater repeater;
    Framebuffer fb{};
    bool fb_ready = false;
    bool settings_dirty = false; // Edited settings not yet written to config.ini.

    void Rescan() {
        games = ScanGames();
        settings = GetMenuSettings();
        ApplyFilter();
    }

    // Switch tabs persisting any pending edits when leaving the settings page so a
    // disk write happens once per editing session rather than once per adjustment.
    void SetTab(Tab next) {
        if (tab == Tab::Settings && next != Tab::Settings) {
            FlushSettings();
        }
        tab = next;
    }

    void FlushSettings() {
        if (settings_dirty) {
            SetMenuSettings(settings);
            settings_dirty = false;
        }
    }

    void ApplyFilter() {
        filtered.clear();
        const std::string needle = ToLowerAscii(search);
        for (int i = 0; i < static_cast<int>(games.size()); ++i) {
            if (needle.empty() || ToLowerAscii(games[i].title).find(needle) != std::string::npos) {
                filtered.push_back(i);
            }
        }
        selected = std::clamp(selected, 0, std::max(0, static_cast<int>(filtered.size()) - 1));
        scroll_row = 0;
    }

    // Returns true if the menu should return `result` to the caller.
    bool HandleLibrary(u64 down, u32 nav, MenuResult& result) {
        const Grid grid = ComputeGrid();
        const int count = static_cast<int>(filtered.size());
        if (count > 0) {
            if (nav & DirLeft) {
                selected = std::max(0, selected - 1);
            }
            if (nav & DirRight) {
                selected = std::min(count - 1, selected + 1);
            }
            if (nav & DirUp) {
                selected = std::max(0, selected - grid.cols);
            }
            if (nav & DirDown) {
                selected = std::min(count - 1, selected + grid.cols);
            }
            if (down & HidNpadButton_A) {
                result = {MenuAction::Launch, games[filtered[selected]].path};
                return true;
            }
        }
        if (down & HidNpadButton_X) {
            search = PromptSearch(search);
            ApplyFilter();
        }
        if (down & HidNpadButton_Y) {
            // Rescanning the SD card blocks
            ShowBusy("Refreshing library…");
            search.clear();
            Rescan();
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
        EnsureVisible(grid);
        return false;
    }

    // Move the cursor out to the Library/Settings rail
    void EnterRail() {
        focus = Focus::Rail;
        rail_sel = tab;
    }

    void HandleRail(u64 down, u32 nav) {
        if (nav & DirUp) {
            rail_sel = Tab::Library;
        }
        if (nav & DirDown) {
            rail_sel = Tab::Settings;
        }
        if (down & HidNpadButton_A) {
            SetTab(rail_sel);
            focus = Focus::Content;
        }
        // B cancels back into the section left.
        if (down & HidNpadButton_B) {
            rail_sel = tab;
            focus = Focus::Content;
        }
    }

    bool HandleSettings(u64 down, u32 nav) {
        if (nav & DirUp) {
            settings_sel = std::max(0, settings_sel - 1);
        }
        if (nav & DirDown) {
            settings_sel = std::min(kNumSettings - 1, settings_sel + 1);
        }
        bool changed = false;
        if (nav & DirLeft) {
            CycleSetting(settings, settings_sel, -1);
            changed = true;
        }
        if ((nav & DirRight) || (down & HidNpadButton_A)) {
            CycleSetting(settings, settings_sel, +1);
            changed = true;
        }
        // Edit the local snapshot only and use FlushSettings() later to apply to config.ini.
        if (changed) {
            settings_dirty = true;
        }
        if (down & HidNpadButton_B) {
            EnterRail();
        }
        return false;
    }

    void EnsureVisible(const Grid& grid) {
        if (filtered.empty()) {
            return;
        }
        const int row = selected / grid.cols;
        if (row < scroll_row) {
            scroll_row = row;
        } else if (row >= scroll_row + grid.visible_rows) {
            scroll_row = row - grid.visible_rows + 1;
        }
    }

    void HandleTouch() {
        HidTouchScreenState ts{};
        if (hidGetTouchScreenStates(&ts, 1) == 0 || ts.count == 0) {
            touch_was_down = false;
            return;
        }
        const int tx = static_cast<int>(ts.touches[0].x);
        const int ty = static_cast<int>(ts.touches[0].y);
        if (touch_was_down) {
            return; // Act on the initial contact only.
        }
        touch_was_down = true;

        if (tx < kRailW) {
            SetTab(ty < kScreenH / 2 ? Tab::Library : Tab::Settings);
            rail_sel = tab;
            focus = Focus::Content;
            return;
        }
        if (tab == Tab::Library) {
            const Grid grid = ComputeGrid();
            for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
                int tile_x, tile_y;
                if (!TileRect(grid, i, tile_x, tile_y)) {
                    continue;
                }
                if (tx >= tile_x && tx < tile_x + kTileW && ty >= tile_y &&
                    ty < tile_y + kTileH) {
                    if (selected == i) {
                        pending_launch = games[filtered[i]].path; // Second tap launches.
                    }
                    selected = i;
                    EnsureVisible(grid);
                    break;
                }
            }
        } else {
            const int row = (ty - (kContentTop + 16)) / (kRowH + 8);
            if (row >= 0 && row < kNumSettings) {
                settings_sel = row;
                CycleSetting(settings, settings_sel, tx > kContentX + kContentW / 2 ? +1 : -1);
                settings_dirty = true;
            }
        }
    }

    // Screen rect of filtered tile `i` given the current scroll.
    bool TileRect(const Grid& grid, int i, int& out_x, int& out_y) {
        const int row = i / grid.cols;
        const int col = i % grid.cols;
        const int y = grid.top + (row - scroll_row) * (kTileH + kTileGap);
        if (row < scroll_row || row >= scroll_row + grid.visible_rows) {
            return false;
        }
        out_x = grid.start_x + col * (kTileW + kTileGap);
        out_y = y;
        return true;
    }

    void Draw() {
        Canvas& c = canvas;
        c.Clear(kColBg);
        DrawRail(c, tab, rail_sel, focus == Focus::Rail);
        if (tab == Tab::Library) {
            DrawLibrary(c);
        } else {
            DrawSettingsPage(c);
        }
        DrawNotice(c);
        DrawHintBar(c);
    }

    void DrawLibrary(Canvas& c) {
        std::string sub;
        if (!search.empty()) {
            sub = "Search: " + search + "  (" + std::to_string(filtered.size()) + ")";
        } else {
            sub = std::to_string(games.size()) + (games.size() == 1 ? " game" : " games");
        }
        DrawHeader(c, sub);

        const bool content_focus = focus == Focus::Content;
        if (filtered.empty()) {
            DrawEmptyLibrary(c);
        } else {
            const Grid grid = ComputeGrid();
            for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
                int x, y;
                if (TileRect(grid, i, x, y)) {
                    DrawTile(c, games[filtered[i]], x, y, i == selected, content_focus);
                }
            }
            DrawScrollbar(c, grid);
        }
        if (focus == Focus::Rail) {
            DrawRailHints(c);
        } else {
            int hx = kContentX + 24;
            const int hy = kContentBottom + (kHintH - 26) / 2;
            hx += DrawHint(c, hx, hy, "A", "Launch") + 22;
            hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
            hx += DrawHint(c, hx, hy, "X", "Search") + 22;
            hx += DrawHint(c, hx, hy, "Y", "Refresh") + 22;
            DrawHint(c, hx, hy, "+ -", "Exit");
        }
    }

    void DrawScrollbar(Canvas& c, const Grid& grid) {
        const int total_rows = (static_cast<int>(filtered.size()) + grid.cols - 1) / grid.cols;
        if (total_rows <= grid.visible_rows) {
            return;
        }
        const int track_h = grid.visible_rows * (kTileH + kTileGap) - kTileGap;
        const int track_x = kScreenW - 10;
        c.FillRoundRect(track_x, grid.top, 4, track_h, 2, kColRail);
        const int thumb_h = std::max(24, track_h * grid.visible_rows / total_rows);
        const int max_scroll = total_rows - grid.visible_rows;
        const int thumb_y = grid.top + (track_h - thumb_h) * scroll_row / std::max(1, max_scroll);
        c.FillRoundRect(track_x, thumb_y, 4, thumb_h, 2, kColAccent);
    }

    static constexpr int kRowH = 52;

    void DrawSettingsPage(Canvas& c) {
        DrawHeader(c, "");
        const bool content_focus = focus == Focus::Content;
        const auto rows = BuildSettingRows(settings);
        const int x = kContentX + 24;
        const int w = kContentW - 48;
        int y = kContentTop + 16;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const bool on = i == settings_sel;
            if (on) {
                c.FillRoundRect(x, y, w, kRowH, 10, content_focus ? kColSurfaceHi : kColSurface);
                c.FillRoundRect(x, y + 8, 4, kRowH - 16, 2,
                                content_focus ? kColAccent : kColBadge);
            }
            g_font.Draw(c, x + 20, CenterBaseline(y, kRowH, 22), rows[i].label, 22, kColText);
            const int vw = g_font.Measure(rows[i].value, 22);
            g_font.Draw(c, x + w - 24 - vw, CenterBaseline(y, kRowH, 22), rows[i].value, 22,
                        on && content_focus ? kColAccent : kColTextDim);
            y += kRowH + 8;
        }
        y += 8;
        const std::string backend = std::string{"Graphics backend: "} + BackendName(settings.graphics_api);
        g_font.Draw(c, x + 20, y + 16, backend, 18, kColTextDim);
        g_font.Draw(c, x + 20, y + 42, "Changes apply the next time you launch a game.", 18,
                    kColTextDim);

        if (focus == Focus::Rail) {
            DrawRailHints(c);
        } else {
            int hx = kContentX + 24;
            const int hy = kContentBottom + (kHintH - 26) / 2;
            hx += DrawHint(c, hx, hy, "<>", "Change") + 22;
            hx += DrawHint(c, hx, hy, "A", "Next") + 22;
            hx += DrawHint(c, hx, hy, "B", "Menu") + 22;
            DrawHint(c, hx, hy, "+ -", "Exit");
        }
    }

    void DrawHintBar(Canvas& c) {
        c.FillRect(0, kContentBottom, kScreenW, kHintH, kColHintBar);
        c.FillRect(0, kContentBottom, kScreenW, 1, kColRail);
    }

    // Legend shown while the cursor sits on the Library/Settings rail.
    void DrawRailHints(Canvas& c) {
        int hx = kContentX + 24;
        const int hy = kContentBottom + (kHintH - 26) / 2;
        hx += DrawHint(c, hx, hy, "^v", "Move") + 22;
        hx += DrawHint(c, hx, hy, "A", "Open") + 22;
        hx += DrawHint(c, hx, hy, "B", "Back") + 22;
        DrawHint(c, hx, hy, "+ -", "Exit");
    }

    // Full-frame busy indicator for the brief blocking scans.
    void ShowBusy(std::string_view msg) {
        Draw();
        canvas.FillRect(0, 0, kScreenW, kScreenH, MakeColor(0x10, 0x11, 0x13, 0xB0));
        const int tw = g_font.Measure(msg, 22);
        const int w = tw + 56, h = 56;
        const int x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
        canvas.FillRoundRect(x, y, w, h, 12, kColSurfaceHi);
        g_font.Draw(canvas, x + 28, CenterBaseline(y, h, 22), msg, 22, kColText);
        Present();
    }

    void EnsureFramebuffer() {
        if (fb_ready) {
            return;
        }
        framebufferCreate(&fb, nwindowGetDefault(), kScreenW, kScreenH, PIXEL_FORMAT_RGBA_8888, 2);
        framebufferMakeLinear(&fb);
        fb_ready = true;
    }

    void DrawLoading() {
        canvas.Clear(kColBg);
        const char* msg = "Loading library...";
        const int w = g_font.Measure(msg, 24);
        g_font.Draw(canvas, kContentX + (kContentW - w) / 2, kScreenH / 2, msg, 24, kColTextDim);
    }

    void Present() {
        EnsureFramebuffer();
        u32 stride = 0;
        auto* base = static_cast<u8*>(framebufferBegin(&fb, &stride));
        const u32* src = canvas.Data();
        for (int y = 0; y < kScreenH; ++y) {
            std::memcpy(base + static_cast<std::size_t>(y) * stride, src + y * kScreenW,
                        static_cast<std::size_t>(kScreenW) * 4);
        }
        framebufferEnd(&fb);
    }

    Canvas canvas;
    bool touch_was_down = false;
    // Set by a second touch on the focused tile so Run() can escape the loop.
    std::optional<std::string> pending_launch;
};

} // namespace

MenuResult RunMenu(PadState& pad) {
    if (!g_font.Init()) {
        // Exit on no font found.
        return {MenuAction::Exit, {}};
    }
    Menu menu;
    return menu.Run(pad);
}

void SetMenuNotice(const std::string& text) {
    g_notice = text;
    g_notice_frames = 240; // ~4 seconds at 60 fps.
}

void ShutdownMenu() {
    g_font.Shutdown();
}

} // namespace SwitchFrontend
