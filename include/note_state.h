// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "voice_logic.h"
#include <array>

namespace notes {
constexpr size_t capacity = 7;
constexpr size_t textBytes = voice::maxCharacters * 4 + 1;
constexpr uint32_t version = 0x4e345604;
struct Entry {
    std::string text, timestamp;
};
enum class View : uint8_t { Empty, Note, Message };
struct State {
    std::array<Entry, capacity> entries;
    uint8_t head = 0, count = 0, selected = 0;
    View view = View::Empty;
    bool screenKnown = false;
    std::string previous;
    Entry message;
    const Entry &current() const { return entries[(head + selected) % capacity]; }
    std::string visibleText() const {
        return view == View::Note ? current().text : view == View::Message ? message.text : "";
    }
};
bool valid(const State &state);
State append(const State &before, const std::string &text, const std::string &stamp);
State newer(const State &before);
State remove(const State &before);
// Compact, bounded serialization; no ABI-dependent new structures in NVS.
std::vector<uint8_t> encode(const State &state);
bool decode(const uint8_t *data, size_t size, State &state);
bool migrateLegacy(const uint8_t *data, size_t size, State &state);
bool load(State &state);
bool save(const State &state);
}
