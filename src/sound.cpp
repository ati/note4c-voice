// SPDX-License-Identifier: GPL-3.0-only
#include "sound.h"
#include <algorithm>
#include <cstring>

namespace {
uint32_t le(const uint8_t *p, unsigned bytes) {
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= uint32_t(p[i]) << (8 * i);
    return value;
}
}
bool sound::parseWav(const uint8_t *file, size_t size, Wave &wave) {
    wave = {};
    if (!file || size < 12 || std::memcmp(file, "RIFF", 4) || std::memcmp(file + 8, "WAVE", 4)) return false;
    const size_t riff = le(file + 4, 4);
    if (riff < 4 || riff > size - 8) return false;
    const size_t end = riff + 8;
    size_t offset = 12;
    bool format = false, data = false;
    Wave result;
    while (offset < end) {
        if (end - offset < 8) return false;
        const auto *id = file + offset;
        const size_t count = le(id + 4, 4);
        offset += 8;
        if (count > end - offset) return false;
        const auto *payload = file + offset;
        if (!std::memcmp(id, "fmt ", 4)) {
            if (format || count < 16 || le(payload, 2) != 1 || le(payload + 2, 2) != 1 ||
                le(payload + 4, 4) != sampleRate || le(payload + 8, 4) != sampleRate * 2 ||
                le(payload + 12, 2) != 2 || le(payload + 14, 2) != 16) return false;
            format = true;
        } else if (!std::memcmp(id, "data", 4)) {
            if (data || !count || count % 2 || count / 2 > maxSamples) return false;
            result.pcm = payload; result.samples = count / 2; data = true;
        }
        offset += count;
        if (count % 2) { if (offset == end) return false; ++offset; }
    }
    if (!format || !data) return false;
    wave = result;
    return true;
}
bool sound::playWav(const uint8_t *file, size_t size, Output &output) {
    struct MuteOnExit {
        Output &output;
        ~MuteOnExit() { output.amplifier(false); }
    } mute{output};
    output.amplifier(false);
    Wave wave;
    if (!parseWav(file, size, wave)) return false;
    int16_t stereo[frameSamples * 2] = {};
    auto silence = [&](unsigned frames) {
        std::memset(stereo, 0, sizeof(stereo));
        while (frames--) if (!output.writeStereo(stereo, frameSamples)) return false;
        return true;
    };
    // Establish steady TX flow after an idle period before enabling the PA.
    if (!silence(dmaBufferCount + 2)) return false;
    output.amplifier(true);
    // Keep the stream flowing while the amplifier settles (at least 60 ms).
    if (!silence(3)) return false;
    for (size_t start = 0; start < wave.samples; start += frameSamples) {
        const size_t count = std::min<size_t>(frameSamples, wave.samples - start);
        std::memset(stereo, 0, sizeof(stereo));
        for (size_t i = 0; i < count; ++i) {
            const uint32_t bits = le(wave.pcm + (start + i) * 2, 2);
            const int16_t sample = static_cast<int16_t>(bits < 32768 ? int32_t(bits) : int32_t(bits) - 65536);
            stereo[i * 2] = stereo[i * 2 + 1] = sample;
        }
        if (!output.writeStereo(stereo, frameSamples)) return false;
    }
    // i2s_write returns when data is queued, not when it is audible. Sending
    // more than a full ring of silence drains the complete WAV before muting.
    return silence(dmaBufferCount + 2);
}
