// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstddef>
#include <ctime>
namespace audio {
enum class Result { Ok, NoSpeech, HardwareError, NoMemory };
struct Recording {
    int16_t *pcm = nullptr;
    size_t samples = 0;
    time_t started = 0;
    ~Recording();
    Recording() = default;
    Recording(const Recording &) = delete;
    Recording &operator=(const Recording &) = delete;
};
Result capture(Recording &recording, unsigned volume);
}
