// SPDX-License-Identifier: GPL-3.0-only
// Adapted from trmnl-firmware src/zectrix.cpp, origin/zectrix (see THIRD_PARTY.md).
#include "board.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

namespace {
bool initialized = false;
bool bootPress = false;
bool bootUp = false, bootDown = false;
uint32_t pressedAt = 0;
void output(int pin, int level) {
    const auto gpio = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpio);
    if (rtc_gpio_is_valid_gpio(gpio)) {
        rtc_gpio_hold_dis(gpio);
        rtc_gpio_deinit(gpio);
    }
    digitalWrite(pin, level);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
}
void hold(int pin, int level) {
    const auto gpio = static_cast<gpio_num_t>(pin);
    rtc_gpio_hold_dis(gpio);
    rtc_gpio_init(gpio);
    rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(gpio, level);
    rtc_gpio_hold_en(gpio);
}
}

void board::begin() {
    gpio_deep_sleep_hold_dis();
    output(batteryHold, HIGH);
    output(epdPower, LOW);
    output(audioPower, LOW);
    output(amplifier, LOW);
    output(led, HIGH);
    rtc_gpio_deinit(static_cast<gpio_num_t>(button));
    pinMode(button, INPUT_PULLUP);
    rtc_gpio_deinit(static_cast<gpio_num_t>(downButton));
    pinMode(downButton, INPUT_PULLUP);
    pinMode(settingsButton, INPUT_PULLUP);
    // initVariant runs before setup and network initialization. Preserve the
    // original press even if the user releases it while the device boots.
    if (!initialized) {
        pressedAt = millis();
        bootPress = digitalRead(button) == LOW;
        bootUp = digitalRead(settingsButton) == LOW;
        bootDown = digitalRead(downButton) == LOW;
        initialized = true;
    }
}

bool board::bootButtonPressed() { return bootPress; }
bool board::bootUpPressed() { return bootUp; }
bool board::bootDownPressed() { return bootDown; }
uint32_t board::bootPressedAt() { return pressedAt; }

void board::prepareSleep() {
    hold(epdPower, LOW);
    hold(batteryHold, HIGH);
    // Audio GPIOs are outside the RTC domain; ordinary GPIO hold persists
    // alongside gpio_deep_sleep_hold_en(). Release it on the next boot.
    digitalWrite(amplifier, LOW);
    digitalWrite(audioPower, LOW);
    digitalWrite(led, HIGH);
    gpio_hold_en(static_cast<gpio_num_t>(amplifier));
    gpio_hold_en(static_cast<gpio_num_t>(audioPower));
    gpio_hold_en(static_cast<gpio_num_t>(led));
    gpio_deep_sleep_hold_en();
}

// Arduino calls this before setup(): assert the battery latch as early as possible.
void initVariant() { board::begin(); }
