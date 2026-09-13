// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: Raibatsu (hello@raibatsu.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <switch.h>

#include "citra_switch/config.h"
#include "citra_switch/glyph_atlas.h"

namespace SwitchFrontend {

namespace {

char32_t DecodeUtf8(std::string_view text, std::size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    char32_t codepoint = 0;
    std::size_t extra = 0;

    if ((lead & 0x80) == 0x00) {
        codepoint = lead;
        extra = 0;
    } else if ((lead & 0xE0) == 0xC0) {
        codepoint = lead & 0x1F;
        extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        codepoint = lead & 0x0F;
        extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        codepoint = lead & 0x07;
        extra = 3;
    } else {
        ++index;
        return U'?';
    }

    ++index;
    for (std::size_t i = 0; i < extra && index < text.size(); ++i, ++index) {
        const unsigned char cont = static_cast<unsigned char>(text[index]);
        if ((cont & 0xC0) != 0x80) {
            break;
        }
        codepoint = (codepoint << 6) | (cont & 0x3F);
    }
    return codepoint;
}

} // namespace

struct GlyphAtlas::Impl {
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    // Faces loaded via SetLanguage() - the Switch OS's own shared font(s) for whatever UI
    // language is active. Usually just Standard (Latin/Japanese/Cyrillic), but can be more for
    // Korean/Chinese - see PlSharedFontType. Tried in order after `face` for any glyph it lacks.
    std::vector<FT_Face> fallback_faces;
    bool pl_initialized = false;
    int current_language = -1;
    float pixel_height = 0.0f;
    std::uint32_t pen_x = 2;
    std::uint32_t pen_y = 0;
    std::uint32_t row_height = 0;
    std::unordered_map<char32_t, GlyphMetrics> glyphs;
};

GlyphAtlas::GlyphAtlas() : impl(std::make_unique<Impl>()) {}

GlyphAtlas::~GlyphAtlas() {
    Shutdown();
}

bool GlyphAtlas::Init(GpuCanvas& canvas, float pixel_height) {
    (void)canvas;
    impl->pixel_height = pixel_height;

    if (FT_Init_FreeType(&impl->library) != 0) {
        return false;
    }

    if (FT_New_Face(impl->library, "romfs:/GoogleSansFlex_24pt-Regular.ttf", 0, &impl->face) == 0) {
        FT_Set_Pixel_Sizes(impl->face, 0, static_cast<FT_UInt>(pixel_height));
    } else {
        impl->face = nullptr;
    }

    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        impl->pl_initialized = true;
        // English (SetLanguage_ENUS's ordinal, 1) as a placeholder default - SetLanguage() is
        // called separately once the caller knows the actual UI language (see citra_switch.cpp),
        // this just guarantees `face`/fallback_faces aren't both empty if that call is skipped.
        SetLanguage(1);
    }

    return impl->face != nullptr || !impl->fallback_faces.empty();
}

void GlyphAtlas::SetLanguage(int cfg_language) {
    if (!impl->pl_initialized || impl->current_language == cfg_language) {
        return;
    }
    impl->current_language = cfg_language;

    for (FT_Face face : impl->fallback_faces) {
        FT_Done_Face(face);
    }
    impl->fallback_faces.clear();

    const auto load_font_data = [&](const PlFontData& font_data) {
        FT_Face fallback_face = nullptr;
        if (FT_New_Memory_Face(impl->library, static_cast<const FT_Byte*>(font_data.address),
                               static_cast<FT_Long>(font_data.size), 0, &fallback_face) == 0) {
            FT_Set_Pixel_Sizes(fallback_face, 0, static_cast<FT_UInt>(impl->pixel_height));
            impl->fallback_faces.push_back(fallback_face);
        }
    };

    // Standard ("Japan, US and Europe" per libnx) always loads first, unconditionally, regardless
    // of which UI language is selected - this is the one font this app relied on before
    // per-language loading existed, and 3DS game titles render in their native script no matter
    // what UI language is active (a Japanese title still needs Japanese glyphs even with English
    // UI selected), so it can never be conditional on the language pick below.
    PlFontData standard{};
    if (R_SUCCEEDED(plGetSharedFontByType(&standard, PlSharedFontType_Standard))) {
        load_font_data(standard);
    }

    // libnx's ::SetLanguage enum (qualified here since it'd otherwise be shadowed by this very
    // method's own name) has ordinal values identical to Service::CFG::SystemLanguage's for 0-11
    // (both list Japanese, English, French, German, Italian, Spanish, ChineseSimplified, Korean,
    // Dutch, Portuguese, Russian, ChineseTraditional in that exact order), so this cast is safe as
    // long as cfg_language stays in that range - CycleSetting() already clamps it to [0, 11].
    int set_language = cfg_language;
    if (set_language < 0 || set_language > static_cast<int>(::SetLanguage_ZHTW)) {
        set_language = static_cast<int>(::SetLanguage_ENUS);
    }
    u64 language_code = 0;
    const Result make_code_result =
        setMakeLanguageCode(static_cast<::SetLanguage>(set_language), &language_code);
    if (R_FAILED(make_code_result)) {
        ProbeLog(("setMakeLanguageCode(" + std::to_string(set_language) +
                 ") failed: 0x" + std::to_string(make_code_result))
                     .c_str());
        return;
    }

    // Layers in whatever else this specific language needs on top of Standard - e.g. Korean adds
    // PlSharedFontType_KO, Chinese adds the ChineseSimplified/Traditional (+ Ext) fonts. Standard
    // itself is skipped here since it's already loaded above.
    std::array<PlFontData, PlSharedFontType_Total> fonts{};
    s32 total_fonts = 0;
    const Result get_font_result = plGetSharedFont(
        language_code, fonts.data(), static_cast<s32>(fonts.size()), &total_fonts);
    if (R_FAILED(get_font_result)) {
        ProbeLog(("plGetSharedFont(0x" + std::to_string(language_code) +
                 ") failed: 0x" + std::to_string(get_font_result))
                     .c_str());
        return;
    }
    for (s32 i = 0; i < total_fonts; ++i) {
        const PlFontData& font_data = fonts[static_cast<std::size_t>(i)];
        if (font_data.type == static_cast<u32>(PlSharedFontType_Standard)) {
            continue;
        }
        load_font_data(font_data);
    }
}

void GlyphAtlas::Shutdown() {
    impl->glyphs.clear();
    if (impl->face != nullptr) {
        FT_Done_Face(impl->face);
        impl->face = nullptr;
    }
    for (FT_Face face : impl->fallback_faces) {
        FT_Done_Face(face);
    }
    impl->fallback_faces.clear();
    if (impl->library != nullptr) {
        FT_Done_FreeType(impl->library);
        impl->library = nullptr;
    }
    if (impl->pl_initialized) {
        plExit();
        impl->pl_initialized = false;
    }
    impl->current_language = -1;
    impl->pen_x = 2;
    impl->pen_y = 0;
    impl->row_height = 0;
}

const GlyphMetrics* GlyphAtlas::GetGlyph(GpuCanvas& canvas, char32_t codepoint) {
    const auto it = impl->glyphs.find(codepoint);
    if (it != impl->glyphs.end()) {
        return &it->second;
    }

    FT_Face active_face = nullptr;
    FT_UInt glyph_index = 0;
    if (impl->face != nullptr) {
        glyph_index = FT_Get_Char_Index(impl->face, static_cast<FT_ULong>(codepoint));
        if (glyph_index != 0) {
            active_face = impl->face;
        }
    }
    for (std::size_t i = 0; active_face == nullptr && i < impl->fallback_faces.size(); ++i) {
        glyph_index = FT_Get_Char_Index(impl->fallback_faces[i], static_cast<FT_ULong>(codepoint));
        if (glyph_index != 0) {
            active_face = impl->fallback_faces[i];
        }
    }
    if (active_face == nullptr) {
        const std::uint32_t box_size =
            static_cast<std::uint32_t>(std::max(2.0f, impl->pixel_height * 0.6f));
        GlyphMetrics metrics{};
        metrics.width = static_cast<float>(box_size);
        metrics.height = static_cast<float>(box_size);
        metrics.bearing_x = 1.0f;
        metrics.bearing_y = static_cast<float>(box_size);
        metrics.advance = static_cast<float>(box_size) + 2.0f;

        const AtlasRegion region = ReserveAtlasRegion(canvas, box_size, box_size);
        if (region.u1 > region.u0) {
            std::vector<std::uint8_t> packed(static_cast<std::size_t>(box_size) * box_size * 4, 0);
            constexpr std::uint32_t kBorder = 1;
            for (std::uint32_t row = 0; row < box_size; ++row) {
                for (std::uint32_t col = 0; col < box_size; ++col) {
                    const bool on_border = row < kBorder || col < kBorder ||
                                           row >= box_size - kBorder || col >= box_size - kBorder;
                    if (on_border) {
                        std::uint8_t* texel = packed.data() + (row * box_size + col) * 4;
                        texel[0] = texel[1] = texel[2] = texel[3] = 255;
                    }
                }
            }
            canvas.UpdateAtlasRegion(region.x, region.y, box_size, box_size, packed.data());
            metrics.u0 = region.u0;
            metrics.v0 = region.v0;
            metrics.u1 = region.u1;
            metrics.v1 = region.v1;
        }
        return &impl->glyphs.emplace(codepoint, metrics).first->second;
    }
    if (FT_Load_Glyph(active_face, glyph_index, FT_LOAD_RENDER) != 0) {
        return nullptr;
    }

    const FT_GlyphSlot slot = active_face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;

    GlyphMetrics metrics{};
    metrics.width = static_cast<float>(bitmap.width);
    metrics.height = static_cast<float>(bitmap.rows);
    metrics.bearing_x = static_cast<float>(slot->bitmap_left);
    metrics.bearing_y = static_cast<float>(slot->bitmap_top);
    metrics.advance = static_cast<float>(slot->advance.x) / 64.0f;

    if (bitmap.width > 0 && bitmap.rows > 0) {
        const AtlasRegion region = ReserveAtlasRegion(canvas, bitmap.width, bitmap.rows);
        if (region.u1 > region.u0) {
            std::vector<std::uint8_t> packed(static_cast<std::size_t>(bitmap.width) *
                                             bitmap.rows * 4);
            const std::size_t pitch = static_cast<std::size_t>(std::abs(bitmap.pitch));
            for (unsigned int row = 0; row < bitmap.rows; ++row) {
                for (unsigned int col = 0; col < bitmap.width; ++col) {
                    const std::uint8_t coverage = bitmap.buffer[row * pitch + col];
                    std::uint8_t* texel = packed.data() + (row * bitmap.width + col) * 4;
                    texel[0] = texel[1] = texel[2] = texel[3] = coverage;
                }
            }
            canvas.UpdateAtlasRegion(region.x, region.y, bitmap.width, bitmap.rows, packed.data());

            metrics.u0 = region.u0;
            metrics.v0 = region.v0;
            metrics.u1 = region.u1;
            metrics.v1 = region.v1;
        }
    }

    return &impl->glyphs.emplace(codepoint, metrics).first->second;
}

AtlasRegion GlyphAtlas::ReserveAtlasRegion(GpuCanvas& canvas, std::uint32_t width,
                                           std::uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    // A region wider or taller than the whole atlas can never fit, wrap or not - reject it
    // outright rather than falling into the wrap-to-a-new-row branch below, which only re-checks
    // the Y axis afterward and would otherwise hand back a region whose right edge is actually
    // past the atlas's real width (an out-of-bounds GPU image write once something uploads to
    // it), no different than the fully-legitimate "ran out of room" case below.
    if (width > canvas.AtlasWidth() || height > canvas.AtlasHeight()) {
        return {};
    }
    if (impl->pen_x + width > canvas.AtlasWidth()) {
        impl->pen_x = 0;
        impl->pen_y += impl->row_height + 1;
        impl->row_height = 0;
    }
    if (impl->pen_y + height > canvas.AtlasHeight()) {
        return {};
    }

    const std::uint32_t x = impl->pen_x;
    const std::uint32_t y = impl->pen_y;
    impl->pen_x += width + 1;
    impl->row_height = std::max(impl->row_height, height);

    const float atlas_w = static_cast<float>(canvas.AtlasWidth());
    const float atlas_h = static_cast<float>(canvas.AtlasHeight());
    return AtlasRegion{
        .x = x,
        .y = y,
        .u0 = static_cast<float>(x) / atlas_w,
        .v0 = static_cast<float>(y) / atlas_h,
        .u1 = static_cast<float>(x + width) / atlas_w,
        .v1 = static_cast<float>(y + height) / atlas_h,
    };
}

float GlyphAtlas::LineHeight() const {
    const FT_Face metrics_face =
        impl->face != nullptr ? impl->face
                              : (impl->fallback_faces.empty() ? nullptr : impl->fallback_faces[0]);
    if (metrics_face == nullptr) {
        return impl->pixel_height;
    }
    return static_cast<float>(metrics_face->size->metrics.height) / 64.0f;
}

float GlyphAtlas::Ascender() const {
    const FT_Face metrics_face =
        impl->face != nullptr ? impl->face
                              : (impl->fallback_faces.empty() ? nullptr : impl->fallback_faces[0]);
    if (metrics_face == nullptr) {
        return impl->pixel_height;
    }
    return static_cast<float>(metrics_face->size->metrics.ascender) / 64.0f;
}

float MeasureText(GlyphAtlas& atlas, GpuCanvas& canvas, std::string_view utf8, float scale) {
    float width = 0.0f;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const char32_t codepoint = DecodeUtf8(utf8, index);
        const GlyphMetrics* glyph = atlas.GetGlyph(canvas, codepoint);
        if (glyph != nullptr) {
            width += glyph->advance * scale;
        }
    }
    return width;
}

float DrawText(GpuCanvas& canvas, GlyphAtlas& atlas, float x, float y, std::string_view utf8,
              CanvasColor color, float scale) {
    float pen_x = x;
    const float baseline = y + atlas.Ascender() * scale;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const char32_t codepoint = DecodeUtf8(utf8, index);
        const GlyphMetrics* glyph = atlas.GetGlyph(canvas, codepoint);
        if (glyph == nullptr) {
            continue;
        }
        if (glyph->width > 0.0f && glyph->height > 0.0f) {
            canvas.DrawTexturedQuad(pen_x + glyph->bearing_x * scale, baseline - glyph->bearing_y * scale,
                                    glyph->width * scale, glyph->height * scale, glyph->u0, glyph->v0,
                                    glyph->u1, glyph->v1, color);
        }
        pen_x += glyph->advance * scale;
    }
    return pen_x - x;
}

std::vector<std::string> WrapText(GpuCanvas& canvas, GlyphAtlas& atlas, std::string_view text,
                                  float max_width) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t newline = text.find('\n', pos);
        const std::string_view paragraph =
            text.substr(pos, newline == std::string_view::npos ? std::string_view::npos
                                                                : newline - pos);
        pos = newline == std::string_view::npos ? text.size() + 1 : newline + 1;

        if (paragraph.empty()) {
            lines.emplace_back();
            continue;
        }
        std::size_t word_start = 0;
        std::string current_line;
        while (word_start < paragraph.size()) {
            const std::size_t word_end = paragraph.find(' ', word_start);
            const std::string_view word = paragraph.substr(
                word_start, word_end == std::string_view::npos ? std::string_view::npos
                                                                : word_end - word_start);
            std::string candidate = current_line.empty()
                                        ? std::string(word)
                                        : current_line + " " + std::string(word);
            if (current_line.empty() || MeasureText(atlas, canvas, candidate) <= max_width) {
                current_line = std::move(candidate);
            } else {
                lines.push_back(std::move(current_line));
                current_line = std::string(word);
            }
            word_start = word_end == std::string_view::npos ? paragraph.size() : word_end + 1;
        }
        lines.push_back(std::move(current_line));
    }
    return lines;
}

} // namespace SwitchFrontend
