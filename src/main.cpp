// SPDX-License-Identifier: GPL-3.0-only
#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include "audio.h"
#include "board.h"
#include "canvas.h"
#include "clock_logic.h"
#include "epd.h"
#include "settings.h"
#include "transcription.h"
#include "voice_logic.h"
#include "gesture.h"
#include "notebook.h"

namespace {
Settings settings;
Canvas screen;
class Storage : public notes::Storage {
    bool read(notes::State &state) override { return notes::load(state); }
    bool write(const notes::State &state) override { return notes::save(state); }
} storage;
class Display : public notes::Display {
    bool full(const uint8_t *frame) override { return epd::update(frame); }
} display;
notes::Notebook notebook(screen, storage, display);
constexpr uint32_t runtimeMagic = 0x4e345604;
struct Runtime { uint32_t magic; int64_t nextSync; bool waitRelease; bool frameTrusted; };
RTC_DATA_ATTR Runtime runtime{runtimeMagic, 0, false, false};

std::string timestamp(time_t epoch) {
    char stamp[32] = "";
    if (clock_logic::validTime(epoch)) {
        tm local{}; localtime_r(&epoch, &local);
        strftime(stamp, sizeof(stamp), "%d.%m.%Y · %H:%M", &local);
    }
    return stamp;
}
void failed(const char *stage) {
    Serial.printf("Operation failed: %s; record a new note or reset to restore\n", stage);
    for (int i = 0; i < 3; ++i) {
        digitalWrite(board::led, LOW); delay(100);
        digitalWrite(board::led, HIGH); delay(100);
    }
}
void show(const std::string &message, time_t epoch = 0) {
    if (!notebook.showMessage(message, timestamp(epoch))) failed("message display");
}
void restoreScreen() {
    if (!notebook.restore()) failed("note restore");
}
bool configure() {
    if (!notebook.invalidate()) { failed("save before settings"); return false; }
    setupPortal(settings);
    runtime.nextSync = 0;
    const bool ok = loadSettings(settings);
    applyTimezone(settings);
    return ok;
}
voice::Action wakeAction() {
    voice::WakeGesture gesture(board::bootPressedAt(), board::bootUpPressed(), board::bootDownPressed());
    voice::Action action;
    do {
        action = gesture.update(digitalRead(board::button) == HIGH,
            digitalRead(board::settingsButton) == LOW, digitalRead(board::downButton) == LOW, millis());
        if (action == voice::Action::Waiting) delay(5);
    } while (action == voice::Action::Waiting);
    return action;
}
void startNetwork() {
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setHostname("note4c-voice");
    WiFi.setAutoReconnect(false); WiFi.begin(settings.ssid, settings.password);
}
bool awaitNetwork() {
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(20);
    return WiFi.status() == WL_CONNECTED;
}
void synchronizeTime(bool wait) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!clock_logic::syncDue(time(nullptr), runtime.nextSync)) return;
    configTime(0, 0, settings.ntp, "time.cloudflare.com", "time.google.com");
    applyTimezone(settings); // configTime() resets TZ.
    if (wait) {
        const uint32_t start = millis();
        while (!clock_logic::validTime(time(nullptr)) && millis() - start < 8000) delay(20);
    }
    // Retry on the next interaction if synchronization did not complete.
}
void stopNetwork() {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) runtime.nextSync = time(nullptr) + 3600;
    sntp_stop(); WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);
}
[[noreturn]] void sleep() {
    stopNetwork();
    const uint32_t started = millis();
    uint32_t releasedAt = millis();
    while (millis() - started < 1500) {
        if (digitalRead(board::button) == LOW) releasedAt = millis();
        if (millis() - releasedAt >= 60) break;
        delay(10);
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const bool held = digitalRead(board::button) == LOW;
    runtime.waitRelease = held;
    runtime.frameTrusted = notebook.trusted();
    // A press during upload/EPD is ignored even if still held when finished.
    // Wake on its release, then immediately re-arm for a fresh press.
    rtc_gpio_pullup_en(GPIO_NUM_0); rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, held ? 1 : 0);
    Serial.flush(); board::prepareSleep(); esp_deep_sleep_start();
    while (true) {}
}
}

void setup() {
    board::begin(); Serial.begin(115200); epd::begin();
    bool configured = loadSettings(settings);
    applyTimezone(settings);
    const bool woke = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    Serial.printf("Boot: reset=%d wake=%d button=%d\n", esp_reset_reason(), esp_sleep_get_wakeup_cause(), board::bootButtonPressed());
    if (!woke || runtime.magic != runtimeMagic) runtime = {runtimeMagic, 0, false, false};
    notebook.begin(runtime.frameTrusted);
    if (runtime.waitRelease) { runtime.waitRelease = false; sleep(); }
    const bool requested = woke || board::bootButtonPressed();
    const auto action = requested ? wakeAction() : voice::Action::Waiting;
    Serial.printf("Wake action: %d\n", static_cast<int>(action));
    if (action == voice::Action::Ignore) sleep();
    if (action == voice::Action::Next || action == voice::Action::Delete) {
        // Navigation needs no network, clock synchronization or audio.
        digitalWrite(board::led, LOW);
        const bool ok = action == voice::Action::Delete ? notebook.deleteNote() : notebook.nextNote();
        Serial.printf("Notes: count=%u selected=%u\n", notebook.state().count, notebook.state().selected);
        if (!ok) failed("note navigation");
        sleep();
    }
    if (!configured || action == voice::Action::Settings) {
        configured = configure();
        if (configured) restoreScreen();
        else show("Настройка не завершена. Нажмите круглую кнопку, чтобы настроить Wi-Fi.");
        sleep();
    }
    if (!requested) { restoreScreen(); sleep(); }
    startNetwork();
    if (!clock_logic::validTime(time(nullptr))) {
        if (awaitNetwork()) synchronizeTime(true);
        if (!clock_logic::validTime(time(nullptr))) {
            stopNetwork();
            if (configure() && clock_logic::validTime(time(nullptr))) restoreScreen();
            else show("Не удалось установить время. Удерживайте круглую кнопку 3 секунды для настройки.");
            sleep();
        }
    }
    const time_t attempted = time(nullptr);
    std::string message;
    bool recognized = false;
    time_t eventTime = attempted;
    {
        audio::Recording recording;
        const auto result = audio::capture(recording, settings.volume);
        if (recording.started) eventTime = recording.started;
        switch (result) {
        case audio::Result::NoMemory: message = "Недостаточно памяти для записи звука."; break;
        case audio::Result::HardwareError: message = "Не удалось включить микрофон или динамик. Перезапустите устройство."; break;
        case audio::Result::NoSpeech: message = "Речь не обнаружена. Нажмите кнопку и попробуйте ещё раз."; break;
        case audio::Result::Ok:
            if (!awaitNetwork()) message = voice::unavailable(serverHost(settings));
            else {
                synchronizeTime(false);
                message = transcribe(recording, settings.server, serverHost(settings), &recognized);
            }
            break;
        }
    } // Free PCM before displaying the result and entering sleep.
    stopNetwork();
    if (recognized) {
        if (!notebook.showNote(message, timestamp(eventTime))) failed("new note");
    } else show(message, eventTime);
    sleep();
}
void loop() {}
