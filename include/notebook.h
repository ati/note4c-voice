// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "canvas.h"
#include "note_state.h"

namespace notes {
class Storage {
public:
    virtual ~Storage() = default;
    virtual bool read(State &state) = 0;
    virtual bool write(const State &state) = 0;
};
class Display {
public:
    virtual ~Display() = default;
    virtual bool full(const uint8_t *frame) = 0;
};
class Notebook {
    Canvas &canvas_;
    Storage &storage_;
    Display &display_;
    State state_;
    bool trusted_ = false;
    bool present(State candidate);
public:
    Notebook(Canvas &canvas, Storage &storage, Display &display)
        : canvas_(canvas), storage_(storage), display_(display) {}
    void begin(bool sameSession);
    bool hasNote() const { return state_.count != 0; }
    bool trusted() const { return trusted_; }
    const State &state() const { return state_; }
    bool invalidate();
    bool showNote(const std::string &text, const std::string &timestamp);
    bool showMessage(const std::string &text, const std::string &timestamp = "");
    bool restore();
    bool nextNote();
    bool deleteNote();
};
}
