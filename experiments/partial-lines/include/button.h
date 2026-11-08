// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

// Exactly one event per press; after a blocking display operation a fresh,
// stable release is required. A button held during refresh is never queued.
class Button {
public:
    void disarm(uint32_t now) { armed_ = false; candidateReleased_ = false; since_ = now; }
    bool press(bool released, uint32_t now) {
        if (released != candidateReleased_) {
            candidateReleased_ = released;
            since_ = now;
        }
        if (uint32_t(now - since_) < 40) return false;
        if (released) { armed_ = true; return false; }
        if (!armed_) return false;
        armed_ = false;
        return true;
    }
private:
    bool armed_ = false;
    bool candidateReleased_ = false;
    uint32_t since_ = 0;
};
