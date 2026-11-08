// SPDX-License-Identifier: GPL-3.0-only
#include "probe.h"
#include "button.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

struct Packet { uint8_t command; std::vector<uint8_t> data; };
class FakeIO : public probe::IO {
public:
    std::vector<Packet> packets;
    bool powered = false;
    int opened = 0, closed = 0, refreshes = 0;
    std::string failAt;
    bool open() override { powered = true; ++opened; return failAt != "open"; }
    void close() override { powered = false; ++closed; }
    void command(uint8_t c) override { assert(powered); packets.push_back({c, {}}); }
    void data(const uint8_t *p, size_t n) override {
        assert(powered && !packets.empty());
        packets.back().data.insert(packets.back().data.end(), p, p + n);
    }
    void pause(uint32_t) override {}
    bool idle(const char *stage, uint32_t) override { return failAt != stage; }
    bool refreshCycle(uint32_t) override { ++refreshes; return failAt != "refresh"; }
    void clear() { packets.clear(); refreshes = 0; }
};

static int color(const probe::Frame &frame, int x, int y) {
    return (frame[y * probe::rowBytes + x / 4] >> (6 - (x % 4) * 2)) & 3;
}
static bool inside(probe::Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
static std::vector<uint8_t> commands(const FakeIO &io) {
    std::vector<uint8_t> out;
    for (const auto &p : io.packets) out.push_back(p.command);
    return out;
}

int main() {
    FakeIO io;
    probe::Panel panel(io);
    assert(panel.addLine() == probe::Result::NeedsBase);
    assert(io.opened == 0);
    assert(panel.yellowBase() == probe::Result::Complete);
    assert(panel.ready() && panel.count() == 0 && !io.powered && io.closed == 1);
    assert(commands(io) == std::vector<uint8_t>({0xE9, 0x10, 0x04, 0x12, 0x02, 0x07}));
    assert(io.packets[1].data.size() == 30000);
    assert(std::all_of(io.packets[1].data.begin(), io.packets[1].data.end(), [](uint8_t x) { return x == 0xAA; }));
    assert(io.refreshes == 1);

    // Verify actual emitted SPI payloads and every displayed pixel, including
    // crossings and preservation of all earlier lines and the yellow exterior.
    for (unsigned i = 0; i < probe::lineCount; ++i) {
        io.clear();
        const auto r = probe::line(i);
        assert(probe::valid(r));
        assert(panel.addLine() == probe::Result::Complete);
        assert(panel.count() == i + 1 && !io.powered);
        assert(commands(io) == std::vector<uint8_t>({0xE9, 0x10, 0x83, 0x10, 0x04, 0x12, 0x02, 0x07}));
        assert(io.refreshes == 1); // No full-refresh fallback before/after probe.
        const auto &window = io.packets[2].data;
        assert(window.size() == 9 && window[8] == 1);
        assert((window[0] * 256 + window[1]) == r.x);
        assert((window[2] * 256 + window[3]) == r.x + r.w - 1);
        assert((window[4] * 256 + window[5]) == r.y);
        assert((window[6] * 256 + window[7]) == r.y + r.h - 1);
        const auto &areaBytes = io.packets[3].data;
        assert(areaBytes.size() == size_t(r.w * r.h / 4));
        assert(std::all_of(areaBytes.begin(), areaBytes.end(), [](uint8_t x) { return x == 0; }));
        assert(io.packets[1].data == std::vector<uint8_t>(panel.frame().begin(), panel.frame().end()));
        for (int y = 0; y < probe::height; ++y) {
            for (int x = 0; x < probe::width; ++x) {
                bool black = false;
                for (unsigned j = 0; j <= i; ++j) black |= inside(probe::line(j), x, y);
                assert(color(panel.frame(), x, y) == (black ? 0 : 2));
            }
        }
    }
    io.clear();
    assert(panel.addLine() == probe::Result::Exhausted);
    assert(io.packets.empty() && io.refreshes == 0); // No automatic clear at end.
    assert(!probe::valid(probe::line(probe::lineCount)));
    assert(!probe::valid({1, 0, 8, 8}));
    assert(!probe::valid({392, 0, 16, 8}));
    assert(!probe::valid({0, 299, 8, 2}));
    assert(!probe::valid({0, 0, 0, 8}));
    assert(panel.yellowBase() == probe::Result::Complete && panel.count() == 0);
    assert(std::all_of(panel.frame().begin(), panel.frame().end(), [](uint8_t x) { return x == 0xAA; }));

    // Any communication timeout must power the panel down, stop the probe,
    // and require an explicit new baseline. Never drive a fallback frame.
    for (const auto *stage : {"open", "OTP init", "frame RAM write", "window RAM write", "power on", "refresh", "power off"}) {
        FakeIO failing;
        probe::Panel test(failing);
        assert(test.yellowBase() == probe::Result::Complete);
        failing.clear();
        failing.failAt = stage;
        assert(test.addLine() == probe::Result::Failed);
        assert(!failing.powered && failing.closed == 2);
        assert(!test.ready() && test.count() == 0 && failing.refreshes <= 1);
        const auto trace = commands(failing);
        if (std::string(stage) == "refresh")
            assert(std::find(trace.begin(), trace.end(), 0x02) == trace.end());
        failing.clear();
        assert(test.addLine() == probe::Result::NeedsBase && failing.packets.empty());
        failing.failAt.clear();
        assert(test.yellowBase() == probe::Result::Complete);
    }
    {
        FakeIO failing;
        failing.failAt = "refresh";
        probe::Panel test(failing);
        assert(test.yellowBase() == probe::Result::Failed);
        assert(!failing.powered && !test.ready());
        assert(test.addLine() == probe::Result::NeedsBase);
    }

    Button b;
    b.disarm(0);
    assert(!b.press(false, 1000)); // Boot press held through first refresh.
    assert(!b.press(true, 1010));
    assert(!b.press(false, 1020)); // Release bounce does not arm.
    assert(!b.press(true, 1100));
    assert(!b.press(true, 1140));
    assert(!b.press(false, 1200));
    assert(b.press(false, 1240));
    assert(!b.press(false, 1400));
    b.disarm(3000); // Press made during display update must be released first.
    assert(!b.press(false, 4000));
    assert(!b.press(true, 4100));
    assert(!b.press(true, 4140));
    assert(!b.press(false, 4200));
    assert(b.press(false, 4240));
    b.disarm(0xFFFFFF00U);
    assert(!b.press(true, 0xFFFFFFD0U));
    assert(!b.press(true, 0xFFFFFFF8U));
    assert(!b.press(false, 0xFFFFFFF9U));
    assert(b.press(false, 0x21U));
    std::puts("PASS: pixels, SPI windows, one drive per click, error shutdown, button debounce");
}
