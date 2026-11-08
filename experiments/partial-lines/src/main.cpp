// SPDX-License-Identifier: GPL-3.0-only
#include <Arduino.h>
#include <SPI.h>
#include "board.h"
#include "button.h"
#include "probe.h"

namespace {
class Hardware : public probe::IO {
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
        digitalWrite(board::epdCs, HIGH);
        delay(10);
        digitalWrite(board::epdReset, HIGH); delay(10);
        digitalWrite(board::epdReset, LOW); delay(20);
        digitalWrite(board::epdReset, HIGH); delay(10);
        if (!idle("reset", 60000)) return false;
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
        Serial.printf("CMD %02X\n", value);
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
        const uint32_t start = millis();
        while (digitalRead(board::epdBusy) == LOW) {
            if (uint32_t(millis() - start) >= timeoutMs) {
                Serial.printf("ERROR: BUSY timeout at %s\n", stage);
                return false;
            }
            delay(1);
        }
        return true;
    }
    bool refreshCycle(uint32_t timeoutMs) override {
        const uint32_t start = millis();
        while (digitalRead(board::epdBusy) != LOW) {
            if (uint32_t(millis() - start) >= 2000) {
                Serial.println("ERROR: no BUSY assertion after display command");
                return false;
            }
            delay(1);
        }
        const uint32_t asserted = millis();
        Serial.printf("BUSY asserted after %lu ms\n", static_cast<unsigned long>(asserted - start));
        if (!idle("display drive", timeoutMs)) return false;
        Serial.printf("BUSY released; drive duration %lu ms\n", static_cast<unsigned long>(millis() - asserted));
        return true;
    }
private:
    SPIClass spi_{HSPI};
    bool transaction_ = false;
};

Hardware hardware;
probe::Panel panel(hardware);
Button front, up;

void disarmButtons() {
    front.disarm(millis());
    up.disarm(millis());
    // Discard serial input received during a display operation, too.
    while (Serial.available()) Serial.read();
}
void result(probe::Result value, uint32_t started) {
    digitalWrite(board::led, HIGH);
    if (value == probe::Result::Complete) {
        Serial.printf("COMMAND_CYCLE_COMPLETE in %lu ms. Lines requested: %u/%u\n",
                      static_cast<unsigned long>(millis() - started), panel.count(), probe::lineCount);
        Serial.println("VISUAL CHECK REQUIRED: black line? yellow preserved? flashing outside window?");
    } else if (value == probe::Result::Exhausted) {
        Serial.println("All 10 lines requested. UP or 'y' starts a new yellow base with a FULL refresh.");
    } else {
        Serial.println("STOPPED: baseline is untrusted. UP or 'y' restores yellow with a FULL refresh.");
    }
    disarmButtons();
    Serial.println("READY: front / 'n' = next line; UP / 'y' = FULL yellow reset; '?' = help");
}
void yellow() {
    Serial.println("BASELINE: FULL yellow refresh (flashing expected)");
    digitalWrite(board::led, LOW);
    const uint32_t started = millis();
    result(panel.yellowBase(), started);
}
void next() {
    if (panel.ready() && panel.count() < probe::lineCount) {
        const auto r = probe::line(panel.count());
        Serial.printf("PROBE %u: %s, x=%d y=%d width=%d height=%d, 0x83 native color OTP\n",
                      panel.count() + 1, panel.count() % 2 ? "vertical" : "horizontal", r.x, r.y, r.w, r.h);
        Serial.printf("RAM only: 30000-byte complete image, then %d-byte window; ONE drive command\n", r.w * r.h / 4);
    }
    digitalWrite(board::led, LOW);
    const uint32_t started = millis();
    result(panel.addLine(), started);
}
}

void setup() {
    board::begin();
    Serial.begin(115200);
    // Bounded USB enumeration window; the test also starts without a monitor.
    delay(1200);
    Serial.println("Note4C partial-lines 1.0: native COLOR OTP WINDOW EXPERIMENT");
    Serial.println("Panel HE042A268; partial-window support is NOT yet validated.");
    Serial.println("Observe the complete screen; BUSY completion is not proof of partial refresh.");
    hardware.begin();
    yellow();
}

void loop() {
    const uint32_t now = millis();
    // Give the explicit baseline reset priority if both buttons are pressed.
    if (up.press(digitalRead(board::settingsButton) == HIGH, now)) yellow();
    else if (front.press(digitalRead(board::button) == HIGH, now)) next();
    if (Serial.available()) {
        const int c = Serial.read();
        if (c == 'n') next();
        else if (c == 'y') yellow();
        else if (c == '?') Serial.println("Front/n: next H/V black line. UP/y: FULL yellow reset. RESET: restart. No Wi-Fi; no deep sleep.");
    }
    delay(5);
}
