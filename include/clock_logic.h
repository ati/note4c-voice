// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <ctime>

namespace clock_logic {
constexpr int64_t minimumEpoch = 1704067200; // 2024-01-01 UTC
constexpr int64_t maximumEpoch = 2147483647; // Pinned Arduino 2.x uses signed 32-bit time_t.
constexpr int64_t syncInterval = 3600;
constexpr int64_t retryInterval = 900;

inline bool validTime(int64_t epoch) {
    return epoch >= minimumEpoch && epoch < maximumEpoch;
}

inline void formatTime(const tm &local, char (&text)[6]) {
    text[0] = '0' + local.tm_hour / 10;
    text[1] = '0' + local.tm_hour % 10;
    text[2] = ':';
    text[3] = '0' + local.tm_min / 10;
    text[4] = '0' + local.tm_min % 10;
    text[5] = '\0';
}

inline uint64_t untilNextMinute(int64_t seconds, int32_t microseconds) {
    return static_cast<uint64_t>(60 - seconds % 60) * 1000000ULL - microseconds;
}

inline bool syncDue(int64_t now, int64_t nextAttempt) {
    return !validTime(now) || nextAttempt == 0 || now >= nextAttempt ||
           nextAttempt - now > syncInterval;
}
} // namespace clock_logic
