// SPDX-License-Identifier: GPL-3.0-only
#include "note_state.h"
#include <Preferences.h>

bool notes::load(State &state) {
    Preferences nvs;
    if (!nvs.begin("note4c-voice", true)) return false;
    const size_t size = nvs.getBytesLength("notes");
    // Seven maximum-length notes, screen history and an optional service message.
    if (!size || size > 6500) { nvs.end(); return false; }
    std::vector<uint8_t> bytes(size);
    const bool found = nvs.getBytes("notes", bytes.data(), size) == size;
    nvs.end();
    return found && (decode(bytes.data(), size, state) || migrateLegacy(bytes.data(), size, state));
}
bool notes::save(const State &state) {
    const auto bytes = encode(state);
    if (bytes.empty()) return false;
    Preferences nvs;
    if (!nvs.begin("note4c-voice", false)) return false;
    const bool ok = nvs.putBytes("notes", bytes.data(), bytes.size()) == bytes.size();
    nvs.end(); return ok;
}
