// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>

namespace panel {
constexpr int width = 400, height = 300, rowBytes = width / 4;
struct Rect { int x, y, w, h; };
bool valid(Rect area);
Rect aligned(Rect area);

class IO {
public:
    virtual ~IO() = default;
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual void command(uint8_t value) = 0;
    virtual void data(const uint8_t *bytes, size_t size) = 0;
    virtual void pause(uint32_t ms) = 0;
    virtual bool idle(const char *stage, uint32_t timeoutMs) = 0;
    virtual bool refreshCycle(uint32_t timeoutMs) = 0;
};

// Native color OTP for both paths. A full RAM write does not drive the panel.
// Partial requests have exactly one window and one drive, with no full fallback.
bool refresh(IO &io, const uint8_t *frame, const Rect *window = nullptr);
}
