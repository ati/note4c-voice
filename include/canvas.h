// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "font.h"
#include "panel.h"
#include <string>
#include <vector>
class Canvas {
public:
    static constexpr int width = 400, height = 300;
    static constexpr size_t byteCount = width * height / 4;
    void clear();
    void pixel(int x, int y);
    void rect(int x, int y, int w, int h);
    bool black(int x, int y) const;
    const uint8_t *data() const { return pixels_; }
    bool note(const std::string &text, const std::string &timestamp, const std::string &previous = "");
    static std::string singleLine(const std::string &text, const Font &font, int width);
    void text(const std::string &value, int x, int y, const Font &font);
    static int measure(const std::vector<uint32_t> &text, const Font &font);
    static std::vector<std::vector<uint32_t>> wrap(const std::vector<uint32_t> &text, const Font &font, int width);
private:
    uint8_t pixels_[byteCount];
};
