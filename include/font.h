// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstddef>
struct Glyph {
    uint32_t codepoint, offset;
    uint8_t width, height, advance;
    int8_t left, top;
};
struct Font {
    uint8_t size, lineHeight, baseline;
    const Glyph *glyphs;
    size_t count;
    const uint8_t *bitmap;
};
extern const Font bodyFonts[];
extern const size_t bodyFontCount;
extern const Font smallFont, stampFont, timeFont;
