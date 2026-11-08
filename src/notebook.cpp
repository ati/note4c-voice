// SPDX-License-Identifier: GPL-3.0-only
#include "notebook.h"

void notes::Notebook::begin(bool sameSession) {
    State candidate;
    state_ = State{};
    if (storage_.read(candidate) && valid(candidate)) state_ = candidate;
    trusted_ = sameSession && state_.screenKnown;
}
bool notes::Notebook::invalidate() {
    trusted_ = false;
    if (!state_.screenKnown) return true;
    State candidate = state_;
    candidate.screenKnown = false;
    if (!storage_.write(candidate)) return false;
    state_ = candidate;
    return true;
}
bool notes::Notebook::present(State candidate) {
    if (!valid(candidate)) return false;
    if (candidate.view == View::Empty) canvas_.clear();
    else {
        const Entry &e = candidate.view == View::Note ? candidate.current() : candidate.message;
        if (!canvas_.note(e.text, e.timestamp, candidate.previous)) return false;
    }
    if (!invalidate() || !display_.full(canvas_.data())) return false;
    candidate.screenKnown = true;
    if (!storage_.write(candidate)) return false;
    state_ = candidate; trusted_ = true;
    return true;
}
bool notes::Notebook::showNote(const std::string &text, const std::string &timestamp) {
    return present(append(state_, text, timestamp));
}
bool notes::Notebook::showMessage(const std::string &text, const std::string &timestamp) {
    State candidate = state_;
    candidate.previous = state_.visibleText();
    candidate.message.text = voice::limitText(text);
    candidate.message.timestamp = timestamp;
    candidate.view = View::Message;
    // Service messages are screen history, but never entries in the ring.
    return present(candidate);
}
bool notes::Notebook::restore() {
    State candidate = state_;
    if (candidate.view == View::Message) {
        candidate.previous = candidate.count ? state_.visibleText() : "";
        candidate.view = candidate.count ? View::Note : View::Empty;
        candidate.message = {};
    }
    return present(candidate);
}
bool notes::Notebook::nextNote() {
    if (trusted_ && (state_.view == View::Empty || (state_.view == View::Note && state_.count == 1))) return true;
    return present(newer(state_));
}
bool notes::Notebook::deleteNote() {
    if (trusted_ && state_.view == View::Empty) return true;
    // An error/help screen is not a deletable note: first return to the saved selection.
    if (state_.view == View::Message) return restore();
    return present(remove(state_));
}
