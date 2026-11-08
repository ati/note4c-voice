// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "voice_logic.h"
#include <cstring>
#include <string>

// Stream the same body on ESP32 and in the host integration check without
// copying the entire PCM buffer or base64 encoding it.
class MultipartBody {
    std::string prefix_, suffix_;
    uint8_t wav_[44];
    const uint8_t *pcm_;
    size_t pcmBytes_, position_ = 0;
public:
    static constexpr const char *boundary() { return "Note4CVoice7dc6a680019c4321"; }
    MultipartBody(const int16_t *pcm, size_t samples) : pcm_(reinterpret_cast<const uint8_t *>(pcm)), pcmBytes_(samples * 2) {
        for (const auto &field : {std::make_pair("model", "whisper-1"), std::make_pair("language", "ru"), std::make_pair("response_format", "json")}) {
            prefix_ += std::string("--") + boundary() + "\r\nContent-Disposition: form-data; name=\"" + field.first + "\"\r\n\r\n" + field.second + "\r\n";
        }
        prefix_ += std::string("--") + boundary() + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
        suffix_ = std::string("\r\n--") + boundary() + "--\r\n";
        voice::wavHeader(wav_, samples);
    }
    size_t size() const { return prefix_.size() + sizeof(wav_) + pcmBytes_ + suffix_.size(); }
    size_t available() const { return size() - position_; }
    size_t read(uint8_t *output, size_t count) {
        count = std::min(count, available());
        size_t copied = 0, offset = position_;
        const uint8_t *parts[] = {reinterpret_cast<const uint8_t *>(prefix_.data()), wav_, pcm_, reinterpret_cast<const uint8_t *>(suffix_.data())};
        const size_t sizes[] = {prefix_.size(), sizeof(wav_), pcmBytes_, suffix_.size()};
        for (size_t p = 0; p < 4 && copied < count; ++p) {
            if (offset >= sizes[p]) { offset -= sizes[p]; continue; }
            const size_t take = std::min(sizes[p] - offset, count - copied);
            std::memcpy(output + copied, parts[p] + offset, take); copied += take; offset = 0;
        }
        position_ += copied; return copied;
    }
};
