// SPDX-License-Identifier: GPL-3.0-only
#include "probe.h"

namespace {
struct Session {
    probe::IO &io;
    ~Session() { io.close(); }
};
}

probe::Rect probe::line(unsigned index) {
    if (index >= lineCount) return {0, 0, 0, 0};
    const int step = static_cast<int>(index / 2);
    return (index % 2 == 0) ? Rect{24, 56 + 40 * step, 352, 8}
                            : Rect{64 + 64 * step, 24, 8, 252};
}

bool probe::valid(Rect r) {
    return r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 &&
           r.x < width && r.y < height && r.w <= width - r.x &&
           r.h <= height - r.y && r.x % 8 == 0 && r.w % 8 == 0;
}

bool probe::Panel::initialize() {
    if (!io_.open()) return false;
    // Established Note4C color startup. Keep the panel's native OTP waveform.
    io_.command(0xE9); byte(0x01);
    io_.pause(10);
    return io_.idle("OTP init", 60000);
}

bool probe::Panel::writeFrame() {
    io_.command(0x10);
    if (!io_.idle("frame RAM write", 60000)) return false;
    for (int y = 0; y < height; ++y) {
        io_.data(frame_.data() + y * rowBytes, rowBytes);
        if (y % 16 == 15) io_.pause(1);
    }
    return true;
}

bool probe::Panel::writeWindow(Rect r) {
    if (!valid(r)) return false;
    // 0x83 format and 8-pixel X alignment from Zectrix's SSD2683 reference.
    // Its use with a COLOR OTP waveform is the hypothesis under test.
    // No monochrome transition codes, PSR override or external LUT are used.
    const int x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    const uint8_t window[] = {
        uint8_t(r.x >> 8), uint8_t(r.x), uint8_t(x1 >> 8), uint8_t(x1),
        uint8_t(r.y >> 8), uint8_t(r.y), uint8_t(y1 >> 8), uint8_t(y1), 0x01
    };
    io_.command(0x83); io_.data(window, sizeof(window));
    io_.command(0x10);
    if (!io_.idle("window RAM write", 60000)) return false;
    for (int y = r.y; y <= y1; ++y) {
        io_.data(frame_.data() + y * rowBytes + r.x / 4, r.w / 4);
        if ((y - r.y) % 16 == 15) io_.pause(1);
    }
    return true;
}

bool probe::Panel::drive() {
    io_.command(0x04);
    io_.pause(10);
    if (!io_.idle("power on", 60000)) return false;
    io_.command(0x12); byte(0x00);
    // Every physical change requires a drive cycle. Only window selection
    // distinguishes this experiment from the initial full-screen refresh.
    if (!io_.refreshCycle(180000)) return false;
    io_.command(0x02); byte(0x00);
    io_.pause(10);
    if (!io_.idle("power off", 60000)) return false;
    io_.pause(20);
    io_.command(0x07); byte(0xA5);
    return true;
}

probe::Result probe::Panel::yellowBase() {
    ready_ = false;
    count_ = 0;
    frame_.fill(0xAA); // 10 = yellow, four pixels per byte.
    Session session{io_};
    if (!initialize() || !writeFrame() || !drive()) return Result::Failed;
    ready_ = true;
    return Result::Complete;
}

probe::Result probe::Panel::addLine() {
    if (!ready_) return Result::NeedsBase;
    if (count_ >= lineCount) return Result::Exhausted;
    const Rect r = line(count_);
    if (!valid(r)) { ready_ = false; return Result::Failed; }
    ready_ = false;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x / 4; x < (r.x + r.w) / 4; ++x)
            frame_[y * rowBytes + x] = 0; // 00 = black, no padding pixels.
    Session session{io_};
    // Reset loses controller RAM. Seed the whole expected image WITHOUT
    // refreshing, then request one narrow window. This prevents uninitialized
    // RAM outside the window from confounding a failed partial-mode probe.
    // A full-screen RAM transfer is not a full-screen optical refresh.
    if (!initialize() || !writeFrame() || !writeWindow(r) || !drive())
        return Result::Failed; // No automatic full-refresh fallback.
    ++count_;
    ready_ = true;
    return Result::Complete;
}
