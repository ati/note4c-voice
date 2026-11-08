// SPDX-License-Identifier: GPL-3.0-only
#include "notebook.h"
#include "gesture.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

namespace {
using Frame = std::array<uint8_t, Canvas::byteCount>;
const std::string stamp = "01.11.2026 · 18:42";
const std::string previous = "Забрать посылку из пункта выдачи по дороге домой.";
const std::string current = "Завтра в десять встреча с командой.";
Frame copy(const uint8_t *data) { Frame f; std::copy(data, data + f.size(), f.begin()); return f; }
void save(const std::string &path, const uint8_t *data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data), Canvas::byteCount);
    assert(out.good());
}
struct Storage : notes::Storage {
    notes::State saved;
    unsigned writes = 0, failWrite = UINT_MAX;
    bool read(notes::State &s) override { s = saved; return true; }
    bool write(const notes::State &s) override {
        if (++writes == failWrite) return false;
        assert(notes::valid(s)); saved = s; return true;
    }
};
struct Display : notes::Display {
    Frame image{};
    unsigned fulls = 0;
    bool failFull = false;
    bool full(const uint8_t *frame) override {
        ++fulls; if (failFull) return false;
        image = copy(frame); return true;
    }
};
void gestures() {
    using voice::Action;
    voice::WakeGesture tap(100, false, false);
    assert(tap.update(false, false, false, 110) == Action::Waiting);
    assert(tap.update(true, false, false, 200) == Action::Waiting);
    assert(tap.update(false, false, false, 215) == Action::Waiting);
    assert(tap.update(true, false, false, 250) == Action::Waiting);
    assert(tap.update(true, false, false, 289) == Action::Waiting);
    assert(tap.update(true, false, false, 290) == Action::Record);
    assert(tap.update(false, true, true, 5000) == Action::Record);
    for (unsigned mask = 0; mask < 4; ++mask) {
        const bool up = mask & 1, down = mask & 2;
        const Action expected = up && down ? Action::Ignore : down ? Action::Delete : up ? Action::Next : Action::Record;
        voice::WakeGesture early(0, up, down);
        assert(early.update(true, false, false, 50) == Action::Waiting);
        assert(early.update(true, false, false, 90) == expected);
        voice::WakeGesture late(0, false, false);
        assert(late.update(false, up, down, 20) == Action::Waiting);
        assert(late.update(true, false, false, 30) == Action::Waiting);
        assert(late.update(true, false, false, 70) == expected);
        voice::WakeGesture held(0, up, down);
        assert(held.update(false, up, down, 2999) == Action::Waiting);
        assert(held.update(false, up, down, 3000) == Action::Settings);
    }
    voice::WakeGesture released(0, false, false);
    assert(released.update(true, true, true, 2999) == Action::Waiting);
    assert(released.update(true, true, true, 3039) == Action::Record);
    voice::WakeGesture wrap(0xfffffff0U, false, true);
    assert(wrap.update(false, false, true, uint32_t(0xfffffff0U + 2999U)) == Action::Waiting);
    assert(wrap.update(false, false, true, uint32_t(0xfffffff0U + 3000U)) == Action::Settings);
}
void layout(const std::string &dir) {
    Canvas c;
    assert(c.note(current, stamp, previous));
    save(dir + "/screen-history.raw", c.data());
    for (int x = 0; x < 400; ++x) {
        const bool dot = x >= 24 && x < 376 && (x - 24) % 2 == 0;
        assert(c.black(x, 43) == dot && c.black(x, 248) == dot);
    }
    assert(Canvas::singleLine("Коротко", stampFont, 352) == "Коротко");
    const auto shortOld = Canvas::singleLine(previous, stampFont, 352);
    assert(shortOld.substr(shortOld.size() - 3) == "...");
    assert(Canvas::measure(voice::decode(shortOld), stampFont) <= 352);
    assert(Canvas::singleLine(previous, stampFont, 0).empty());
    for (const std::string unit : {"Щ", "i", "🙂", "я "}) {
        std::string value;
        for (unsigned n = 0; n < 160; ++n) value += unit;
        assert(c.note(voice::limitText(value), stamp, previous));
        for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x)
            if (c.black(x, y)) assert(x >= 24 && x < 376 &&
                ((y >= 14 && y <= 43) || (y >= 56 && y < 230) || (y >= 248 && y < 290)));
        if (unit == "Щ") save(dir + "/screen-history-wide.raw", c.data());
    }
    assert(c.note(current, stamp));
    for (int x = 0; x < 400; ++x) assert(!c.black(x, 43));
    c.clear();
    for (size_t i = 0; i < Canvas::byteCount; ++i) assert(c.data()[i] == 0xaa);
    save(dir + "/screen-empty.raw", c.data());
}
void notebook(const std::string &dir) {
    Canvas c; Storage storage; Display display;
    notes::Notebook book(c, storage, display);
    book.begin(false);
    assert(book.restore());
    const auto emptyFulls = display.fulls;
    assert(book.nextNote() && book.deleteNote() && display.fulls == emptyFulls);
    assert(book.showNote("A", "A time") && book.state().previous.empty());
    assert(book.showNote("B", "B time") && book.state().previous == "A");
    assert(book.showNote("C", "C time") && book.state().previous == "B");
    assert(book.nextNote() && book.state().current().text == "A" && book.state().previous == "C");
    assert(book.nextNote() && book.state().current().text == "B" && book.state().previous == "A");
    assert(book.deleteNote() && book.state().current().text == "A" && book.state().previous == "B");
    assert(book.state().current().timestamp == "A time");
    assert(book.deleteNote() && book.state().current().text == "C" && book.state().previous == "A");
    const auto oneFulls = display.fulls;
    assert(book.nextNote() && display.fulls == oneFulls && book.state().previous == "A");
    assert(book.deleteNote() && !book.hasNote() && book.state().previous.empty());
    assert(std::all_of(display.image.begin(), display.image.end(), [](uint8_t p) { return p == 0xaa; }));
    notes::Notebook wake(c, storage, display);
    wake.begin(true); assert(wake.trusted() && !wake.hasNote());
    assert(wake.showNote(previous, stamp) && wake.state().previous.empty());
    assert(wake.showNote(current, stamp));
    assert(wake.deleteNote() && wake.state().previous == current);
    save(dir + "/screen-after-delete.raw", display.image.data());
    assert(wake.invalidate() && wake.restore()); // settings retain header and selection
    assert(wake.state().current().text == previous && wake.state().previous == current);
    notes::Notebook cold(c, storage, display);
    cold.begin(false); assert(!cold.trusted());
    assert(cold.restore() && cold.state().previous == current);
    assert(cold.showMessage(voice::unavailable("whisper.local"), stamp));
    assert(cold.state().count == 1 && cold.state().previous == previous);
    assert(cold.deleteNote() && cold.state().count == 1); // never delete a hidden note behind an error
    assert(cold.state().previous == voice::unavailable("whisper.local"));
    assert(cold.showMessage("Ошибка"));
    assert(cold.showNote("Новая", stamp) && cold.state().previous == "Ошибка" && cold.state().count == 2);
    assert(cold.showNote("Ещё", stamp));
    assert(cold.nextNote()); // oldest selected; new recordings still append at chronological end
    assert(cold.showNote("Самая новая", stamp));
    assert(cold.state().current().text == "Самая новая" && cold.state().previous == previous);

    const auto old = notes::encode(storage.saved);
    storage.failWrite = storage.writes + 1;
    const auto fulls = display.fulls;
    assert(!cold.deleteNote() && display.fulls == fulls && notes::encode(storage.saved) == old);
    storage.failWrite = UINT_MAX;
    assert(cold.restore());
    display.failFull = true;
    const auto oldText = cold.state().current().text;
    assert(!cold.deleteNote() && !cold.trusted() && !storage.saved.screenKnown);
    assert(storage.saved.current().text == oldText && storage.saved.count == 4);
    display.failFull = false;
    assert(cold.restore());
    storage.failWrite = storage.writes + 2;
    assert(!cold.showNote("Не сохранено", stamp) && !cold.trusted());
    assert(storage.saved.current().text == oldText && !storage.saved.screenKnown);
    storage.failWrite = UINT_MAX;
    notes::Notebook recovery(c, storage, display);
    recovery.begin(true); assert(!recovery.trusted());
    assert(recovery.restore() && recovery.state().current().text == oldText);
}
void ringModel() {
    // Independent chronological list model covers wraparound, deletion holes,
    // recording while browsing, serialization and screen history over 10k actions.
    notes::State state;
    std::vector<std::string> model;
    size_t selected = 0;
    std::mt19937 rng(12345);
    for (unsigned step = 0; step < 10000; ++step) {
        const std::string oldScreen = model.empty() ? "" : model[selected];
        const unsigned action = rng() % 3;
        if (action == 0) {
            const auto value = std::to_string(step);
            state = notes::append(state, value, value);
            if (model.size() == 7) model.erase(model.begin());
            model.push_back(value); selected = model.size() - 1;
        } else if (action == 1) {
            state = notes::newer(state);
            if (!model.empty()) selected = (selected + 1) % model.size();
        } else {
            state = notes::remove(state);
            if (!model.empty()) {
                model.erase(model.begin() + selected);
                selected = model.empty() ? 0 : selected ? selected - 1 : model.size() - 1;
            }
        }
        assert(notes::valid(state) && state.count == model.size() && state.selected == selected);
        assert(state.previous == (model.empty() ? "" : oldScreen));
        for (size_t i = 0; i < model.size(); ++i) {
            const auto &e = state.entries[(state.head + i) % notes::capacity];
            assert(e.text == model[i] && e.timestamp == model[i]);
        }
        const auto bytes = notes::encode(state);
        notes::State restored;
        assert(notes::decode(bytes.data(), bytes.size(), restored));
        assert(notes::encode(restored) == bytes);
        if (step % 13 == 0) state = restored;
    }
    state = {};
    for (int i = 0; i < 8; ++i) state = notes::append(state, std::to_string(i), stamp);
    assert(state.count == 7 && state.current().text == "7");
    state = notes::newer(state); assert(state.current().text == "1");
    state = notes::remove(state); assert(state.current().text == "7");
}
void persistence() {
    notes::State s;
    std::string maximum;
    for (int i = 0; i < 160; ++i) maximum += "🙂";
    for (unsigned i = 0; i < 8; ++i) s = notes::append(s, maximum, stamp);
    s.view = notes::View::Message; s.message.text = maximum; s.message.timestamp = stamp;
    const auto bytes = notes::encode(s);
    assert(bytes.size() < 6500);
    notes::State restored;
    assert(notes::decode(bytes.data(), bytes.size(), restored));
    for (size_t n = 0; n < bytes.size(); ++n) assert(!notes::decode(bytes.data(), n, restored));
    for (size_t pos = 0; pos < 8; ++pos) {
        auto bad = bytes; bad[pos] = 255;
        assert(!notes::decode(bad.data(), bad.size(), restored));
    }
    auto trailing = bytes; trailing.push_back(0);
    assert(!notes::decode(trailing.data(), trailing.size(), restored));
    s.entries[s.head].text = std::string("A\0B", 3); assert(!notes::valid(s));
    // Legacy snapshot uses real offsets, including the unaligned uint16 field padding.
    std::array<uint8_t, 1324> old{};
    old[0] = 3; old[1] = 0x56; old[2] = 0x34; old[3] = 0x4e;
    std::memcpy(old.data() + 4, current.c_str(), current.size() + 1);
    std::memcpy(old.data() + 645, previous.c_str(), previous.size() + 1);
    std::memcpy(old.data() + 1286, stamp.c_str(), stamp.size() + 1);
    old[1318] = 3; old[1320] = 1;
    assert(notes::migrateLegacy(old.data(), old.size(), restored));
    assert(restored.count == 2 && restored.current().text == current);
    assert(restored.current().timestamp == stamp && restored.entries[0].timestamp.empty());
    assert(restored.previous == previous && !restored.screenKnown);
    assert(notes::decode(notes::encode(restored).data(), notes::encode(restored).size(), s));
    old[0] = 1; assert(!notes::migrateLegacy(old.data(), old.size(), restored));
    old[0] = 3; old[1318] = 17; assert(!notes::migrateLegacy(old.data(), old.size(), restored));
    old[1318] = 0; std::fill(old.begin() + 4, old.begin() + 645, 'x');
    assert(!notes::migrateLegacy(old.data(), old.size(), restored));
}
struct Packet { uint8_t command; std::vector<uint8_t> bytes; };
struct Wire : panel::IO {
    std::vector<Packet> packets;
    unsigned opens = 0, closes = 0, waits = 0, drives = 0, failWait = UINT_MAX;
    bool failOpen = false, failDrive = false;
    bool open() override { ++opens; return !failOpen; }
    void close() override { ++closes; }
    void command(uint8_t v) override { packets.push_back({v, {}}); }
    void data(const uint8_t *v, size_t n) override {
        assert(!packets.empty()); packets.back().bytes.insert(packets.back().bytes.end(), v, v + n);
    }
    void pause(uint32_t) override {}
    bool idle(const char *, uint32_t) override { return ++waits != failWait; }
    bool refreshCycle(uint32_t timeout) override { ++drives; assert(timeout == 180000); return !failDrive; }
};
void protocol() {
    Canvas c; assert(c.note(current, stamp, previous));
    const Frame f = copy(c.data());
    const panel::Rect r{25, 117, 16, 2}; // Unaligned request spans three bytes groups.
    Wire w; assert(panel::refresh(w, f.data(), &r));
    assert(w.opens == 1 && w.closes == 1 && w.drives == 1 && w.packets.size() == 8);
    const std::vector<uint8_t> commands{0xe9, 0x10, 0x83, 0x10, 0x04, 0x12, 0x02, 0x07};
    for (size_t i = 0; i < commands.size(); ++i) assert(w.packets[i].command == commands[i]);
    assert(w.packets[0].bytes == std::vector<uint8_t>{1});
    assert(w.packets[1].bytes == std::vector<uint8_t>(f.begin(), f.end()));
    assert(w.packets[2].bytes == (std::vector<uint8_t>{0, 24, 0, 47, 0, 117, 0, 118, 1}));
    std::vector<uint8_t> expected;
    for (int y = 117; y <= 118; ++y) expected.insert(expected.end(), f.begin() + y * 100 + 6, f.begin() + y * 100 + 12);
    assert(w.packets[3].bytes == expected);
    assert(w.packets[5].bytes == std::vector<uint8_t>{0});
    assert(w.packets[6].bytes == std::vector<uint8_t>{0});
    assert(w.packets[7].bytes == std::vector<uint8_t>{0xa5});
    Wire full; assert(panel::refresh(full, f.data()));
    assert(full.packets.size() == 6 && full.drives == 1);
    for (const auto &p : full.packets) assert(p.command != 0x83);
    for (unsigned stage = 1; stage <= 5; ++stage) {
        Wire failed; failed.failWait = stage;
        assert(!panel::refresh(failed, f.data(), &r));
        assert(failed.closes == 1 && failed.drives <= 1);
    }
    Wire start; start.failOpen = true;
    assert(!panel::refresh(start, f.data(), &r) && start.closes == 1 && start.packets.empty());
    Wire busy; busy.failDrive = true;
    assert(!panel::refresh(busy, f.data(), &r) && busy.closes == 1 && busy.drives == 1);
    for (auto invalid : {panel::Rect{-1, 0, 8, 2}, {0, 0, 0, 2}, {399, 299, 2, 2}, {0, 0, INT_MAX, 1}}) {
        Wire no; assert(!panel::refresh(no, f.data(), &invalid) && no.opens == 0);
    }
    const auto edge = panel::aligned({399, 299, 1, 1});
    assert(edge.x == 392 && edge.w == 8 && edge.y == 299 && edge.h == 1);
    Wire null; assert(!panel::refresh(null, nullptr) && null.opens == 0);
}
}
int main(int argc, char **argv) {
    assert(argc == 2);
    gestures(); layout(argv[1]); notebook(argv[1]); ringModel(); persistence(); protocol();
    std::cout << "PASS: seven-note ring (10000 actions), navigation/deletion, previous screen, blank screen, NVS migration/validation, failure recovery, gestures, panel protocol\n";
}
