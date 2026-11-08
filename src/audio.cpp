// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include "board.h"
#include "voice_logic.h"
#include "sound.h"
#include <Arduino.h>
#include <driver/i2c.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include "es8311.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr auto port = I2S_NUM_0;
extern const uint8_t startWav[] asm("_binary_assets_sounds_record_start_wav_start");
extern const uint8_t startWavEnd[] asm("_binary_assets_sounds_record_start_wav_end");
extern const uint8_t stopWav[] asm("_binary_assets_sounds_record_stop_wav_start");
extern const uint8_t stopWavEnd[] asm("_binary_assets_sounds_record_stop_wav_end");
static_assert(voice::sampleRate == sound::sampleRate && voice::frameSamples == sound::frameSamples, "I2S formats must match");
struct Session {
    bool i2c = false, i2s = false;
    es8311_handle_t codec = nullptr;
    ~Session() {
        digitalWrite(board::amplifier, LOW);
        if (codec) { es8311_voice_mute(codec, true); es8311_delete(codec); }
        if (i2s) i2s_driver_uninstall(port);
        if (i2c) i2c_driver_delete(I2C_NUM_0);
        for (int pin : {board::audioTx, board::audioMclk, board::audioBclk, board::audioWs,
                        board::i2cSda, board::i2cScl}) {
            pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
        }
        digitalWrite(board::audioPower, LOW);
        digitalWrite(board::led, HIGH);
    }
    bool begin(unsigned volume) {
        digitalWrite(board::amplifier, LOW);
        digitalWrite(board::audioPower, HIGH);
        delay(30);
        i2c_config_t bus{};
        bus.mode = I2C_MODE_MASTER;
        bus.sda_io_num = board::i2cSda; bus.scl_io_num = board::i2cScl;
        bus.sda_pullup_en = GPIO_PULLUP_ENABLE; bus.scl_pullup_en = GPIO_PULLUP_ENABLE;
        bus.master.clk_speed = 100000;
        if (i2c_param_config(I2C_NUM_0, &bus) != ESP_OK ||
            i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return false;
        i2c = true;
        i2s_config_t config{};
        config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
        config.sample_rate = voice::sampleRate;
        config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        config.dma_buf_count = sound::dmaBufferCount; config.dma_buf_len = voice::frameSamples;
        config.tx_desc_auto_clear = true;
        config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
        if (i2s_driver_install(port, &config, 0, nullptr) != ESP_OK) return false;
        i2s = true;
        i2s_pin_config_t pins{};
        pins.mck_io_num = board::audioMclk; pins.bck_io_num = board::audioBclk;
        pins.ws_io_num = board::audioWs; pins.data_out_num = board::audioTx;
        pins.data_in_num = board::audioRx;
        if (i2s_set_pin(port, &pins) != ESP_OK) return false;
        i2s_zero_dma_buffer(port);
        codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
        if (!codec) return false;
        es8311_clock_config_t clock{};
        clock.mclk_from_mclk_pin = true;
        clock.mclk_frequency = voice::sampleRate * 256;
        clock.sample_frequency = voice::sampleRate;
        return es8311_init(codec, &clock, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK &&
            es8311_microphone_config(codec, false) == ESP_OK &&
            es8311_microphone_gain_set(codec, ES8311_MIC_GAIN_30DB) == ESP_OK &&
            es8311_voice_volume_set(codec, volume, nullptr) == ESP_OK &&
            es8311_voice_mute(codec, false) == ESP_OK;
    }
};
bool readFrame(int16_t (&frame)[voice::frameSamples * 2]) {
    size_t read = 0;
    return i2s_read(port, frame, sizeof(frame), &read, pdMS_TO_TICKS(500)) == ESP_OK && read == sizeof(frame);
}
class Speaker : public sound::Output {
public:
    void amplifier(bool enabled) override { digitalWrite(board::amplifier, enabled ? HIGH : LOW); }
    bool writeStereo(const int16_t *frame, size_t count) override {
        size_t written = 0;
        return i2s_write(port, frame, count * 4, &written, pdMS_TO_TICKS(1000)) == ESP_OK && written == count * 4;
    }
};
}

audio::Recording::~Recording() { free(pcm); }
audio::Result audio::capture(Recording &recording, unsigned volume) {
    if (recording.pcm) return Result::HardwareError;
    recording.pcm = static_cast<int16_t *>(heap_caps_malloc(voice::maxSamples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!recording.pcm) return Result::NoMemory;
    Session session;
    if (!session.begin(volume)) return Result::HardwareError;
    int16_t stereo[voice::frameSamples * 2];
    voice::HighPass filters[2];
    float channelNoise[2] = {};
    // Discard startup transients, then calibrate both slots. ES8311 transmits
    // one mono slot; measuring both avoids relying on legacy DMA slot ordering.
    for (int frame = 0; frame < 25; ++frame) {
        if (!readFrame(stereo)) return Result::HardwareError;
        float sums[2] = {};
        for (unsigned i = 0; i < voice::frameSamples; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                const float sample = filters[ch].process(stereo[i * 2 + ch]);
                sums[ch] += sample * sample;
            }
        if (frame >= 5) for (int ch = 0; ch < 2; ++ch) channelNoise[ch] += std::sqrt(sums[ch] / voice::frameSamples) / 20;
    }
    const int channel = channelNoise[1] > channelNoise[0] ? 1 : 0;
    voice::CapturePolicy policy(channelNoise[channel]);
    Serial.printf("Audio slot %d; noise %.0f; threshold %.0f\n", channel, channelNoise[channel], policy.threshold());
    Speaker speaker;
    Serial.println("Playing record-start.wav");
    if (!sound::playWav(startWav, startWavEnd - startWav, speaker)) return Result::HardwareError;
    // Clear RX packets containing the start tone and its immediate echo.
    delay(60);
    for (int n = 0; n < 20; ++n) {
        size_t bytes = 0;
        if (i2s_read(port, stereo, sizeof(stereo), &bytes, 0) != ESP_OK || !bytes) break;
    }
    voice::HighPass filter;
    voice::StopButton button;
    recording.started = time(nullptr);
    Serial.println("Recording started after cue playback");
    const uint32_t startedMs = millis();
    digitalWrite(board::led, LOW);
    voice::StopReason reason = voice::StopReason::None;
    while (reason == voice::StopReason::None) {
        if (!readFrame(stereo)) return Result::HardwareError;
        float sum = 0;
        for (unsigned i = 0; i < voice::frameSamples; ++i) {
            const int16_t sample = filter.process(stereo[i * 2 + channel]);
            recording.pcm[recording.samples++] = sample;
            sum += static_cast<float>(sample) * sample;
        }
        const bool stop = button.update(digitalRead(board::button) == HIGH, millis());
        reason = policy.add(std::sqrt(sum / voice::frameSamples), voice::frameSamples, stop);
        if (millis() - startedMs > 32000) return Result::HardwareError;
    }
    digitalWrite(board::led, HIGH);
    // The buffer length is fixed now: completion sounds cannot enter the WAV.
    Serial.println("Recording stopped; playing record-stop.wav");
    if (!sound::playWav(stopWav, stopWavEnd - stopWav, speaker)) return Result::HardwareError;
    Serial.printf("Captured %u samples; stop reason %d\n", static_cast<unsigned>(recording.samples), static_cast<int>(reason));
    return reason == voice::StopReason::NoSpeech ? Result::NoSpeech : Result::Ok;
}
