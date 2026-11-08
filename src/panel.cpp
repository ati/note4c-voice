// SPDX-License-Identifier: GPL-3.0-only
// Partial color sequence promoted from the user-validated partial-lines test.
#include "panel.h"

bool panel::valid(Rect r) {
    return r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 &&
        r.x < width && r.y < height && r.w <= width - r.x && r.h <= height - r.y;
}
panel::Rect panel::aligned(Rect r) {
    if (!valid(r)) return {0, 0, 0, 0};
    const int right = (r.x + r.w + 7) & ~7;
    r.x &= ~7;
    r.w = right - r.x;
    return r;
}
bool panel::refresh(IO &io, const uint8_t *frame, const Rect *window) {
    if (!frame || (window && !valid(*window))) return false;
    struct Session { IO &io; ~Session() { io.close(); } } session{io};
    if (!io.open()) return false;
    const auto byte = [&](uint8_t value) { io.data(&value, 1); };
    io.command(0xE9); byte(0x01);
    io.pause(10);
    if (!io.idle("OTP init", 60000)) return false;
    io.command(0x10);
    if (!io.idle("frame RAM write", 60000)) return false;
    for (int y = 0; y < height; ++y) {
        io.data(frame + y * rowBytes, rowBytes);
        if (y % 16 == 15) io.pause(1);
    }
    if (window) {
        const Rect r = aligned(*window);
        const int x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
        // SSD2683 SetPartialWindow format from the Zectrix reference.
        const uint8_t bytes[] = {
            uint8_t(r.x >> 8), uint8_t(r.x), uint8_t(x1 >> 8), uint8_t(x1),
            uint8_t(r.y >> 8), uint8_t(r.y), uint8_t(y1 >> 8), uint8_t(y1), 0x01
        };
        io.command(0x83); io.data(bytes, sizeof(bytes));
        io.command(0x10);
        if (!io.idle("window RAM write", 60000)) return false;
        // Padding pixels retain their original colors; only the requested window is refreshed.
        for (int y = r.y; y <= y1; ++y) {
            io.data(frame + y * rowBytes + r.x / 4, r.w / 4);
            if ((y - r.y) % 16 == 15) io.pause(1);
        }
    }
    io.command(0x04); io.pause(10);
    if (!io.idle("power on", 60000)) return false;
    io.command(0x12); byte(0x00);
    if (!io.refreshCycle(180000)) return false;
    io.command(0x02); byte(0x00); io.pause(10);
    if (!io.idle("power off", 60000)) return false;
    io.pause(20);
    io.command(0x07); byte(0xA5);
    return true;
}
