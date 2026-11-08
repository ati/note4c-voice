// SPDX-License-Identifier: GPL-3.0-only
#include "note_state.h"
#include <cstring>

namespace {
bool validText(const std::string &s) {
    return s.size() < notes::textBytes && s.find('\0') == std::string::npos && voice::limitText(s) == s;
}
bool validEntry(const notes::Entry &e) {
    return validText(e.text) && e.timestamp.size() < 32 && e.timestamp.find('\0') == std::string::npos;
}
void selectView(notes::State &s, const notes::State &before) {
    s.previous = s.count ? before.visibleText() : "";
    s.view = s.count ? notes::View::Note : notes::View::Empty;
    s.message = {};
    s.screenKnown = false;
}
void putString(std::vector<uint8_t> &out, const std::string &s) {
    out.push_back(s.size() & 255); out.push_back(s.size() >> 8);
    out.insert(out.end(), s.begin(), s.end());
}
}
bool notes::valid(const State &s) {
    if (s.head >= capacity || s.count > capacity ||
        (s.count ? s.selected >= s.count : s.selected != 0) ||
        static_cast<unsigned>(s.view) > static_cast<unsigned>(View::Message) ||
        (s.view == View::Note && !s.count) || (s.view == View::Empty && s.count) ||
        (s.view == View::Empty && !s.previous.empty()) ||
        !validText(s.previous) || !validEntry(s.message) ||
        (s.view == View::Message && s.message.text.empty())) return false;
    for (size_t i = 0; i < s.count; ++i) {
        const Entry &e = s.entries[(s.head + i) % capacity];
        if (e.text.empty() || !validEntry(e)) return false;
    }
    return true;
}
notes::State notes::append(const State &before, const std::string &text, const std::string &stamp) {
    State s = before;
    if (s.count == capacity) s.head = (s.head + 1) % capacity;
    else ++s.count;
    s.selected = s.count - 1;
    Entry &e = s.entries[(s.head + s.selected) % capacity];
    e.text = voice::limitText(text); e.timestamp = stamp;
    selectView(s, before);
    return s;
}
notes::State notes::newer(const State &before) {
    State s = before;
    if (s.count) s.selected = (s.selected + 1) % s.count;
    selectView(s, before);
    return s;
}
notes::State notes::remove(const State &before) {
    State s = before;
    if (s.count) {
        for (size_t i = s.selected; i + 1 < s.count; ++i)
            s.entries[(s.head + i) % capacity] = s.entries[(s.head + i + 1) % capacity];
        s.entries[(s.head + s.count - 1) % capacity] = {};
        --s.count;
        s.selected = s.count ? (s.selected ? s.selected - 1 : s.count - 1) : 0;
    }
    selectView(s, before);
    return s;
}
std::vector<uint8_t> notes::encode(const State &s) {
    if (!valid(s)) return {};
    std::vector<uint8_t> out;
    for (unsigned i = 0; i < 4; ++i) out.push_back(version >> (8 * i));
    out.push_back(s.count); out.push_back(s.selected);
    out.push_back(static_cast<uint8_t>(s.view)); out.push_back(s.screenKnown);
    putString(out, s.previous); putString(out, s.message.text); putString(out, s.message.timestamp);
    for (size_t i = 0; i < s.count; ++i) {
        const Entry &e = s.entries[(s.head + i) % capacity];
        putString(out, e.text); putString(out, e.timestamp);
    }
    return out;
}
bool notes::decode(const uint8_t *data, size_t size, State &state) {
    if (!data || size < 8) return false;
    uint32_t format = 0;
    for (unsigned i = 0; i < 4; ++i) format |= uint32_t(data[i]) << (8 * i);
    if (format != version || data[4] > capacity || data[7] > 1) return false;
    State s;
    s.count = data[4]; s.selected = data[5]; s.view = static_cast<View>(data[6]); s.screenKnown = data[7];
    size_t pos = 8;
    auto get = [&](std::string &value, size_t limit) {
        if (size - pos < 2) return false;
        const size_t length = data[pos] | (size_t(data[pos + 1]) << 8); pos += 2;
        if (length >= limit || length > size - pos) return false;
        value.assign(reinterpret_cast<const char *>(data + pos), length); pos += length;
        return true;
    };
    if (!get(s.previous, textBytes) || !get(s.message.text, textBytes) || !get(s.message.timestamp, 32)) return false;
    for (size_t i = 0; i < s.count; ++i)
        if (!get(s.entries[i].text, textBytes) || !get(s.entries[i].timestamp, 32)) return false;
    if (pos != size || !valid(s)) return false;
    state = s; return true;
}
bool notes::migrateLegacy(const uint8_t *data, size_t size, State &state) {
    // Exact ESP32 layout of the October 11 state, including final alignment.
    struct Legacy {
        uint32_t format;
        char current[textBytes], previous[textBytes], timestamp[32];
        uint16_t struck;
        uint8_t screenKnown, reserved;
    } old;
    static_assert(sizeof(Legacy) == 1324, "Legacy NVS layout changed");
    if (!data || size != sizeof(old)) return false;
    std::memcpy(&old, data, size);
    if (old.format != 0x4e345603 || old.struck > 16 || old.screenKnown > 1 ||
        !std::memchr(old.current, 0, textBytes) || !std::memchr(old.previous, 0, textBytes) ||
        !std::memchr(old.timestamp, 0, 32) || !validText(old.current) || !validText(old.previous) ||
        (!old.current[0] && (old.previous[0] || old.struck || old.screenKnown))) return false;
    State s;
    // The old firmware did not retain the previous note's recording time.
    if (old.previous[0]) s = append(s, old.previous, "");
    if (old.current[0]) s = append(s, old.current, old.timestamp);
    state = s; return true;
}
