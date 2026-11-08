// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>

namespace sound {
constexpr unsigned sampleRate = 16000;
constexpr unsigned frameSamples = 320;
constexpr unsigned dmaBufferCount = 6;
constexpr unsigned maxSamples = sampleRate * 5;

struct Wave {
    const uint8_t *pcm = nullptr;
    size_t samples = 0;
};
// PCM16 LE mono / 16 kHz; accepts metadata chunks and RIFF padding.
bool parseWav(const uint8_t *file, size_t size, Wave &wave);

class Output {
public:
    virtual ~Output() = default;
    virtual void amplifier(bool enabled) = 0;
    virtual bool writeStereo(const int16_t *samples, size_t frames) = 0;
};
// Returns only after the WAV has passed through the DMA ring. The PA is
// disabled on success and on every error path. No heap allocation or sleeps.
bool playWav(const uint8_t *file, size_t size, Output &output);
}
