// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace probe {
constexpr int width = 400;
constexpr int height = 300;
constexpr int rowBytes = width / 4;
constexpr unsigned lineCount = 10;
using Frame = std::array<uint8_t, rowBytes * height>;

struct Rect { int x, y, w, h; };
Rect line(unsigned index);
bool valid(Rect area);

// The controller may accept commands without producing the requested optical
// result. Complete only means that the BUSY cycle completed, never optical PASS.
enum class Result { Complete, NeedsBase, Exhausted, Failed };

class IO {
public:
    virtual ~IO() = default;
    virtual bool open() = 0;  // External power, hardware reset, SPI transaction.
    virtual void close() = 0; // Always disable external panel power and SPI.
    virtual void command(uint8_t value) = 0;
    virtual void data(const uint8_t *bytes, size_t size) = 0;
    virtual void pause(uint32_t ms) = 0;
    virtual bool idle(const char *stage, uint32_t timeoutMs) = 0;
    virtual bool refreshCycle(uint32_t timeoutMs) = 0; // Requires BUSY low then high.
};

class Panel {
public:
    explicit Panel(IO &io) : io_(io) {}
    Result yellowBase();
    Result addLine();
    unsigned count() const { return count_; }
    bool ready() const { return ready_; }
    const Frame &frame() const { return frame_; }
private:
    IO &io_;
    Frame frame_{};
    bool ready_ = false;
    unsigned count_ = 0;
    bool initialize();
    bool writeFrame();
    bool writeWindow(Rect area);
    bool drive();
    void byte(uint8_t value) { io_.data(&value, 1); }
};
} // namespace probe
