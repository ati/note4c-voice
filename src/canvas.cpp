// SPDX-License-Identifier: GPL-3.0-only
#include "canvas.h"
#include "voice_logic.h"
#include <algorithm>
#include <cstring>

namespace {
const Glyph &glyph(uint32_t cp, const Font &font) {
    size_t first = 0, last = font.count;
    while (first < last) {
        const size_t mid = (first + last) / 2;
        if (font.glyphs[mid].codepoint < cp) first = mid + 1;
        else last = mid;
    }
    if (first < font.count && font.glyphs[first].codepoint == cp) return font.glyphs[first];
    return glyph('?', font);
}
}
// Native SSD2683 pixel 10 = yellow, repeated four times per byte.
void Canvas::clear() { std::memset(pixels_, 0xaa, sizeof(pixels_)); }
void Canvas::pixel(int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    pixels_[y * (width / 4) + x / 4] &= ~(3U << (6 - (x % 4) * 2));
}
bool Canvas::black(int x, int y) const {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    return ((pixels_[y * (width / 4) + x / 4] >> (6 - (x % 4) * 2)) & 3) == 0;
}
void Canvas::rect(int x, int y, int w, int h) {
    for (int row = std::max(0, y); row < std::min(height, y + h); ++row)
        for (int col = std::max(0, x); col < std::min(width, x + w); ++col) pixel(col, row);
}
int Canvas::measure(const std::vector<uint32_t> &value, const Font &font) {
    int width = 0;
    for (auto cp : value) width += glyph(cp, font).advance;
    return width;
}
void Canvas::text(const std::string &value, int x, int y, const Font &font) {
    for (auto cp : voice::decode(value)) {
        const auto &g = glyph(cp, font);
        const int stride = (g.width + 7) / 8;
        for (int row = 0; row < g.height; ++row)
            for (int col = 0; col < g.width; ++col)
                if (font.bitmap[g.offset + row * stride + col / 8] & (0x80 >> (col % 8)))
                    pixel(x + g.left + col, y + font.baseline + g.top + row);
        x += g.advance;
    }
}
std::vector<std::vector<uint32_t>> Canvas::wrap(const std::vector<uint32_t> &value, const Font &font, int maxWidth) {
    std::vector<std::vector<uint32_t>> lines;
    size_t start = 0;
    while (start < value.size()) {
        while (start < value.size() && value[start] == ' ') ++start;
        if (start == value.size()) break;
        int width = 0;
        size_t end = start, lastSpace = start;
        while (end < value.size() && width + glyph(value[end], font).advance <= maxWidth) {
            width += glyph(value[end], font).advance;
            if (value[end] == ' ') lastSpace = end;
            ++end;
        }
        if (end == start) ++end;
        else if (end < value.size() && lastSpace > start && value[end] != ' ') end = lastSpace;
        size_t trimmed = end;
        while (trimmed > start && value[trimmed - 1] == ' ') --trimmed;
        lines.emplace_back(value.begin() + start, value.begin() + trimmed);
        start = end;
    }
    return lines;
}
std::string Canvas::singleLine(const std::string &value, const Font &font, int maxWidth) {
    auto points = voice::decode(voice::normalize(value));
    if (measure(points, font) <= maxWidth) return voice::encode(points);
    const auto dots = voice::decode("...");
    const int dotsWidth = measure(dots, font);
    if (dotsWidth > maxWidth) return "";
    while (!points.empty() && measure(points, font) + dotsWidth > maxWidth) points.pop_back();
    while (!points.empty() && points.back() == ' ') points.pop_back();
    points.insert(points.end(), dots.begin(), dots.end());
    return voice::encode(points);
}
bool Canvas::note(const std::string &value, const std::string &timestamp, const std::string &previous) {
    clear();
    const bool history = !previous.empty();
    if (history) {
        text(singleLine(previous, stampFont, 352), 24, 14, stampFont);
        for (int x = 24; x < 376; x += 2) pixel(x, 43);
    }
    const int bodyTop = history ? 56 : 27, bodyHeight = history ? 174 : 202;
    const auto points = voice::decode(voice::limitText(value));
    const Font *font = nullptr;
    std::vector<std::vector<uint32_t>> lines;
    for (size_t i = 0; i < bodyFontCount; ++i) {
        lines = wrap(points, bodyFonts[i], 348);
        if (lines.size() * bodyFonts[i].lineHeight <= static_cast<size_t>(bodyHeight)) { font = &bodyFonts[i]; break; }
    }
    if (!font) return false;
    int y = bodyTop + (bodyHeight - static_cast<int>(lines.size()) * font->lineHeight) / 2;
    for (const auto &line : lines) {
        text(voice::encode(line), 24, y, *font);
        y += font->lineHeight;
    }
    if (!timestamp.empty()) {
        for (int x = 24; x < 376; x += 2) pixel(x, 248);
        text("ЗАПИСАНО", 24, 267, smallFont);
        const size_t split = timestamp.rfind(' ');
        if (split != std::string::npos && timestamp.size() - split - 1 == 5) {
            const auto date = timestamp.substr(0, split + 1);
            const auto time = timestamp.substr(split + 1);
            const int dateWidth = measure(voice::decode(date), stampFont);
            const int timeWidth = measure(voice::decode(time), timeFont);
            // Different sizes share one baseline; the date keeps its old size.
            const int baseline = 263 + stampFont.baseline;
            text(date, 376 - dateWidth - timeWidth, 263, stampFont);
            text(time, 376 - timeWidth, baseline - timeFont.baseline, timeFont);
        } else {
            const int stampWidth = measure(voice::decode(timestamp), stampFont);
            text(timestamp, 376 - stampWidth, 263, stampFont);
        }
    }
    return true;
}
