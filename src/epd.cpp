// SPDX-License-Identifier: GPL-3.0-only
// Note4C native color transport; panel.cpp owns the shared refresh protocol.
#include "epd.h"
#include "board.h"
#include <Arduino.h>
#include <SPI.h>

namespace {
class Hardware : public panel::IO {
    SPIClass spi_{HSPI};
    bool transaction_ = false;
public:
    void begin() {
        pinMode(board::epdBusy, INPUT);
        pinMode(board::epdDc, OUTPUT);
        pinMode(board::epdCs, OUTPUT);
        pinMode(board::epdReset, OUTPUT);
        spi_.begin(board::epdSck, -1, board::epdMosi, -1);
    }
    bool open() override {
        digitalWrite(board::epdPower, HIGH);
        digitalWrite(board::epdCs, HIGH); delay(10);
        digitalWrite(board::epdReset, HIGH); delay(10);
        digitalWrite(board::epdReset, LOW); delay(20);
        digitalWrite(board::epdReset, HIGH); delay(10);
        if (!idle("reset", 60000)) return false;
        // Same SPI speed used in the physically validated partial-lines test.
        spi_.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
        transaction_ = true;
        return true;
    }
    void close() override {
        digitalWrite(board::epdCs, HIGH);
        if (transaction_) spi_.endTransaction();
        transaction_ = false;
        digitalWrite(board::epdPower, LOW);
        digitalWrite(board::epdDc, LOW);
        digitalWrite(board::epdReset, LOW);
        digitalWrite(board::epdCs, LOW);
        digitalWrite(board::epdSck, LOW);
        digitalWrite(board::epdMosi, LOW);
    }
    void command(uint8_t value) override {
        digitalWrite(board::epdDc, LOW);
        digitalWrite(board::epdCs, LOW);
        spi_.transfer(value);
        digitalWrite(board::epdCs, HIGH);
    }
    void data(const uint8_t *bytes, size_t size) override {
        digitalWrite(board::epdDc, HIGH);
        digitalWrite(board::epdCs, LOW);
        spi_.transferBytes(bytes, nullptr, size);
        digitalWrite(board::epdCs, HIGH);
    }
    void pause(uint32_t ms) override { delay(ms); }
    bool idle(const char *stage, uint32_t timeoutMs) override {
        const uint32_t started = millis();
        while (digitalRead(board::epdBusy) == LOW) {
            if (uint32_t(millis() - started) >= timeoutMs) {
                Serial.printf("EPD timeout: %s\n", stage);
                return false;
            }
            delay(1);
        }
        return true;
    }
    bool refreshCycle(uint32_t timeoutMs) override {
        const uint32_t started = millis();
        while (digitalRead(board::epdBusy) != LOW) {
            if (uint32_t(millis() - started) >= 2000) {
                Serial.println("EPD error: no BUSY assertion after refresh");
                return false;
            }
            delay(1);
        }
        const uint32_t asserted = millis();
        if (!idle("display drive", timeoutMs)) return false;
        Serial.printf("EPD drive: %lu ms\n", static_cast<unsigned long>(millis() - asserted));
        return true;
    }
};
Hardware hardware;
}

void epd::begin() { hardware.begin(); }
bool epd::update(const uint8_t *framebuffer) {
    return panel::refresh(hardware, framebuffer);
}
bool epd::updateWindow(const uint8_t *framebuffer, panel::Rect window) {
    const auto aligned = panel::aligned(window);
    Serial.printf("EPD window: x=%d y=%d w=%d h=%d\n", aligned.x, aligned.y, aligned.w, aligned.h);
    return panel::refresh(hardware, framebuffer, &window);
}
