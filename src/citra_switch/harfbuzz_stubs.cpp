// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// This is just to make the linker happy.
//
// The proper fix is to install/build `switch-harfbuzz` and link it, but
// being so honest I couldn't be bothered right now.

#include <cstddef>

extern "C" {

void* hb_blob_create(void) {
    return nullptr;
}
void* hb_buffer_create(void) {
    return nullptr;
}
void* hb_face_create(void) {
    return nullptr;
}
void* hb_face_create_for_tables(void) {
    return nullptr;
}
void* hb_font_create(void) {
    return nullptr;
}
void* hb_font_get_face(void) {
    return nullptr;
}
void* hb_set_create(void) {
    return nullptr;
}

void hb_blob_destroy(void) {}
void hb_buffer_destroy(void) {}
void hb_buffer_clear_contents(void) {}
void hb_buffer_add_utf8(void) {}
void hb_buffer_guess_segment_properties(void) {}
void hb_face_destroy(void) {}
void hb_face_set_index(void) {}
void hb_face_set_upem(void) {}
void hb_font_destroy(void) {}
void hb_font_set_scale(void) {}
void hb_shape(void) {}
void hb_ot_layout_collect_lookups(void) {}
void hb_ot_layout_lookup_collect_glyphs(void) {}
void hb_set_destroy(void) {}
void hb_set_subtract(void) {}

unsigned int hb_buffer_get_length(void) {
    return 0;
}
void* hb_buffer_get_glyph_infos(void* /*buffer*/, unsigned int* length) {
    if (length) {
        *length = 0;
    }
    return nullptr;
}
void* hb_buffer_get_glyph_positions(void* /*buffer*/, unsigned int* length) {
    if (length) {
        *length = 0;
    }
    return nullptr;
}
int hb_set_is_empty(void) {
    return 1;
}
int hb_set_next(const void* /*set*/, unsigned int* /*value*/) {
    return 0;
}
int hb_ot_layout_lookup_would_substitute(void) {
    return 0;
}
void hb_ot_tags_from_script_and_language(unsigned int /*script*/, const void* /*language*/,
                                         unsigned int* script_count, unsigned int* /*script_tags*/,
                                         unsigned int* language_count,
                                         unsigned int* /*language_tags*/) {
    if (script_count) {
        *script_count = 0;
    }
    if (language_count) {
        *language_count = 0;
    }
}

} // extern "C"
