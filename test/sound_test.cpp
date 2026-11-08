// SPDX-License-Identifier: GPL-3.0-only
#include "sound.h"
#include "voice_logic.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <vector>

namespace {
std::vector<uint8_t> wav(const std::vector<int16_t> &samples) {
    uint8_t header[44];
    voice::wavHeader(header, samples.size());
    std::vector<uint8_t> data(header, header + 44);
    for (int16_t sample : samples) {
        const uint16_t value = static_cast<uint16_t>(sample);
        data.push_back(value & 255); data.push_back(value >> 8);
    }
    return data;
}
void put32(std::vector<uint8_t> &file, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) file[offset + i] = (value >> (i * 8)) & 255;
}
std::vector<int16_t> samples(const sound::Wave &wave) {
    std::vector<int16_t> result;
    for (size_t i = 0; i < wave.samples; ++i) {
        const uint16_t value = wave.pcm[i * 2] | (uint16_t(wave.pcm[i * 2 + 1]) << 8);
        result.push_back(static_cast<int16_t>(value));
    }
    return result;
}
// Model DMA buffering: a successful write queues data, rather than playing it
// immediately. Muting early would drop the WAV tail and fail the comparison.
struct BufferedSpeaker : sound::Output {
    bool enabled = false;
    size_t failAt = std::numeric_limits<size_t>::max();
    size_t writes = 0, elapsed = 0, enabledAt = 0;
    size_t firstSoundAt = std::numeric_limits<size_t>::max();
    std::deque<std::vector<int16_t>> pending;
    std::vector<int16_t> heard;
    void amplifier(bool value) override {
        if (value) enabledAt = elapsed;
        enabled = value;
    }
    bool writeStereo(const int16_t *frame, size_t count) override {
        if (writes++ == failAt) return false;
        assert(count == sound::frameSamples);
        std::vector<int16_t> mono;
        for (size_t i = 0; i < count; ++i) {
            assert(frame[i * 2] == frame[i * 2 + 1]);
            mono.push_back(frame[i * 2]);
        }
        pending.push_back(mono);
        if (pending.size() > sound::dmaBufferCount) {
            for (int16_t value : pending.front()) {
                if (value) {
                    assert(enabled);
                    if (firstSoundAt == std::numeric_limits<size_t>::max()) firstSoundAt = elapsed;
                }
                if (enabled) heard.push_back(value);
                ++elapsed;
            }
            pending.pop_front();
        }
        return true;
    }
};
void play(const std::vector<uint8_t> &file) {
    sound::Wave wave;
    assert(sound::parseWav(file.data(), file.size(), wave));
    const auto expected = samples(wave);
    BufferedSpeaker speaker;
    assert(sound::playWav(file.data(), file.size(), speaker));
    assert(!speaker.enabled);
    // Only zero padding can remain after the function reports completion.
    for (const auto &chunk : speaker.pending) for (auto value : chunk) assert(value == 0);
    const auto first = std::find_if(expected.begin(), expected.end(), [](int16_t value) { return value != 0; });
    const auto last = std::find_if(expected.rbegin(), expected.rend(), [](int16_t value) { return value != 0; }).base();
    if (first != expected.end()) {
        const auto heardFirst = std::find_if(speaker.heard.begin(), speaker.heard.end(), [](int16_t value) { return value != 0; });
        const auto heardLast = std::find_if(speaker.heard.rbegin(), speaker.heard.rend(), [](int16_t value) { return value != 0; }).base();
        assert(std::vector<int16_t>(first, last) == std::vector<int16_t>(heardFirst, heardLast));
        assert(speaker.firstSoundAt - speaker.enabledAt >= sound::sampleRate * 60 / 1000);
    }
    // Inject a failure at every write: power must be released even mid-WAV.
    for (size_t fail = 0; fail < speaker.writes; ++fail) {
        BufferedSpeaker failed; failed.failAt = fail;
        assert(!sound::playWav(file.data(), file.size(), failed));
        assert(!failed.enabled);
    }
}
void malformed() {
    auto valid = wav({-32768, -100, 0, 100, 32767});
    sound::Wave decoded;
    assert(sound::parseWav(valid.data(), valid.size(), decoded));
    assert(samples(decoded) == std::vector<int16_t>({-32768, -100, 0, 100, 32767}));
    play(valid); // Non-frame-aligned tail must be played and padded.
    assert(!sound::parseWav(nullptr, 0, decoded));
    for (size_t size = 0; size < valid.size(); ++size)
        assert(!sound::parseWav(valid.data(), size, decoded));
    for (const auto field : {20, 22, 24, 28, 32, 34}) {
        auto invalid = valid; invalid[field] ^= 1;
        assert(!sound::parseWav(invalid.data(), invalid.size(), decoded));
    }
    auto invalid = valid; put32(invalid, 4, 0xffffffff);
    assert(!sound::parseWav(invalid.data(), invalid.size(), decoded));
    invalid = valid; put32(invalid, 40, 0xffffffff);
    assert(!sound::parseWav(invalid.data(), invalid.size(), decoded));
    invalid = wav({});
    assert(!sound::parseWav(invalid.data(), invalid.size(), decoded));
    invalid = wav(std::vector<int16_t>(sound::maxSamples + 1));
    assert(!sound::parseWav(invalid.data(), invalid.size(), decoded));
    auto maximum = wav(std::vector<int16_t>(sound::maxSamples));
    assert(sound::parseWav(maximum.data(), maximum.size(), decoded));
    // Real WAV encoders may insert odd-sized metadata before PCM.
    auto metadata = valid;
    const std::vector<uint8_t> junk = {'J','U','N','K',3,0,0,0,1,2,3,0};
    metadata.insert(metadata.begin() + 36, junk.begin(), junk.end());
    put32(metadata, 4, metadata.size() - 8);
    assert(sound::parseWav(metadata.data(), metadata.size(), decoded));
    play(metadata);
    // Arbitrary bytes and lengths must not expose unchecked chunk arithmetic.
    uint32_t seed = 1;
    for (unsigned i = 0; i < 1000; ++i) {
        auto mutation = metadata;
        seed = seed * 1664525U + 1013904223U;
        mutation[seed % mutation.size()] ^= static_cast<uint8_t>(seed >> 24);
        sound::parseWav(mutation.data(), mutation.size(), decoded);
    }
    BufferedSpeaker bad;
    assert(!sound::playWav(nullptr, 0, bad) && !bad.enabled && bad.writes == 0);
}
}
int main(int argc, char **argv) {
    assert(argc == 3);
    malformed();
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        assert(file);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        play(bytes);
        std::cout << "PASS: complete cue playback and mute/error paths: " << argv[i] << '\n';
    }
    std::cout << "PASS: WAV validation, metadata/padding, truncated files, buffered playback, PA warmup and drain\n";
}
