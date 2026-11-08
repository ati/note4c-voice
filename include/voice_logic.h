// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace voice {
constexpr unsigned sampleRate = 16000;
constexpr unsigned frameSamples = 320; // 20 ms.
constexpr unsigned maxSamples = sampleRate * 30;
constexpr unsigned maxCharacters = 160;
inline std::string unavailable(const std::string &host) { return "Сервис транскрибации на " + host + " недоступен"; }

// Reject overlong sequences, surrogates and out-of-range Unicode scalars.
std::vector<uint32_t> decode(const std::string &text);
std::string encode(const std::vector<uint32_t> &text);
std::string normalize(const std::string &text);
std::string limitText(const std::string &text);
void wavHeader(uint8_t (&header)[44], size_t samples);

class HighPass {
    float previousInput_ = 0, previousOutput_ = 0;
public:
    int16_t process(int16_t sample) {
        const float filtered = .97f * (previousOutput_ + sample - previousInput_);
        previousInput_ = sample;
        previousOutput_ = filtered;
        return static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, filtered)));
    }
};

enum class StopReason { None, Silence, Limit, Button, NoSpeech };
class CapturePolicy {
    unsigned samples_ = 0, lastVoice_ = 0, consecutive_ = 0;
    bool speech_ = false;
    float threshold_;
public:
    explicit CapturePolicy(float noiseRms)
        : threshold_(std::max(220.f, std::min(4000.f, noiseRms * 3.f))) {}
    StopReason add(float rms, unsigned count, bool stopPressed) {
        samples_ += count;
        if (rms >= threshold_) {
            consecutive_ += count;
            if (consecutive_ >= sampleRate * 60 / 1000) speech_ = true;
            if (speech_) lastVoice_ = samples_;
        } else consecutive_ = 0;
        if (stopPressed) return speech_ ? StopReason::Button : StopReason::NoSpeech;
        if (samples_ >= maxSamples) return speech_ ? StopReason::Limit : StopReason::NoSpeech;
        if (speech_ && samples_ - lastVoice_ >= sampleRate * 2) return StopReason::Silence;
        if (!speech_ && samples_ >= sampleRate * 5) return StopReason::NoSpeech;
        return StopReason::None;
    }
    float threshold() const { return threshold_; }
};

// Must observe a stable release after wake/ready before accepting a new press.
class StopButton {
    bool state_ = true, raw_ = true, armed_ = false;
    uint32_t changed_ = 0;
public:
    bool update(bool released, uint32_t now) {
        if (released != raw_) { raw_ = released; changed_ = now; }
        if (now - changed_ < 40) return false;
        if (raw_) armed_ = true;
        if (state_ == raw_) return false;
        state_ = raw_;
        if (!state_ && armed_) { armed_ = false; return true; }
        return false;
    }
};
}
