// SPDX-License-Identifier: GPL-3.0-only
#include "voice_logic.h"
#include <cstring>

std::vector<uint32_t> voice::decode(const std::string &text) {
    std::vector<uint32_t> output;
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<uint8_t>(text[i]);
        uint32_t cp = first;
        size_t length = 1;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; length = 2; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; length = 3; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; length = 4; }
        else if (first >= 0x80) cp = 0xfffd;
        bool valid = i + length <= text.size();
        for (size_t j = 1; valid && j < length; ++j) {
            const auto next = static_cast<uint8_t>(text[i + j]);
            valid = (next & 0xc0) == 0x80;
            cp = (cp << 6) | (next & 63);
        }
        if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) ||
            (length == 4 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) valid = false;
        output.push_back(valid ? cp : 0xfffd);
        i += valid ? length : 1;
    }
    return output;
}

std::string voice::encode(const std::vector<uint32_t> &text) {
    std::string out;
    for (uint32_t cp : text) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    return out;
}

std::string voice::normalize(const std::string &text) {
    std::vector<uint32_t> out;
    bool space = false;
    for (auto cp : decode(text)) {
        if (cp <= 32 || cp == 0xa0 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029) {
            space = !out.empty(); continue;
        }
        if (cp == 0x7f || cp == 0x200b || cp == 0xfeff || (cp >= 0x202a && cp <= 0x202e)) continue;
        if (space) out.push_back(' ');
        space = false; out.push_back(cp);
    }
    return encode(out);
}

std::string voice::limitText(const std::string &text) {
    auto out = decode(normalize(text));
    if (out.size() > maxCharacters) { out.resize(maxCharacters - 1); out.push_back(0x2026); }
    return encode(out);
}

void voice::wavHeader(uint8_t (&h)[44], size_t samples) {
    std::memset(h, 0, sizeof(h));
    auto put = [&](int offset, uint32_t value, int bytes) {
        for (int n = 0; n < bytes; ++n) h[offset + n] = (value >> (8 * n)) & 255;
    };
    std::memcpy(h, "RIFF", 4); put(4, 36 + samples * 2, 4);
    std::memcpy(h + 8, "WAVEfmt ", 8); put(16, 16, 4);
    put(20, 1, 2); put(22, 1, 2); put(24, sampleRate, 4); put(28, sampleRate * 2, 4);
    put(32, 2, 2); put(34, 16, 2); std::memcpy(h + 36, "data", 4); put(40, samples * 2, 4);
}
