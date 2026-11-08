// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace voice {
enum class Action { Waiting, Record, Next, Delete, Settings, Ignore };
// Preserve early button samples, resolve after a debounced release.
// A long round press always opens settings; both side buttons cancel a short press.
class WakeGesture {
    uint32_t started_, releasedAt_ = 0;
    bool up_, down_, releasing_ = false;
    Action action_ = Action::Waiting;
public:
    WakeGesture(uint32_t started, bool up, bool down) : started_(started), up_(up), down_(down) {}
    Action update(bool released, bool up, bool down, uint32_t now) {
        if (action_ != Action::Waiting) return action_;
        if (released) {
            if (!releasing_) { releasing_ = true; releasedAt_ = now; }
            if (uint32_t(now - releasedAt_) >= 40)
                action_ = up_ && down_ ? Action::Ignore : down_ ? Action::Delete : up_ ? Action::Next : Action::Record;
        } else {
            releasing_ = false;
            up_ = up_ || up; down_ = down_ || down;
            if (uint32_t(now - started_) >= 3000) action_ = Action::Settings;
        }
        return action_;
    }
};
}
