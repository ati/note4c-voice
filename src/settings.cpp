// SPDX-License-Identifier: GPL-3.0-only
#include "settings.h"
#include "board.h"
#include "canvas.h"
#include "epd.h"
#include "validation.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <sys/time.h>

namespace {
constexpr char storageNamespace[] = "note4c-voice";
String escape(String value) {
    value.replace("&", "&amp;"); value.replace("<", "&lt;"); value.replace(">", "&gt;");
    value.replace("\"", "&quot;"); value.replace("'", "&#39;"); return value;
}
String page(const Settings &s, const String &token) {
    String h = R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Настройка Note4C</title><style>body{font:17px system-ui;background:#f3f2ed;color:#222;max-width:500px;margin:32px auto;padding:0 22px}label{display:block;margin:20px 0 6px}input,button{box-sizing:border-box;width:100%;padding:12px;font:inherit}input[type=checkbox]{width:auto}button{margin-top:24px;background:#222;color:#fff;border:0;border-radius:7px}small{display:block;color:#555;line-height:1.5;margin:8px 0}</style>
<h1>Настройка Note4C</h1><p>Запись фразы по кнопке и распознавание речи.</p>
<form method="post" action="/save" id="setup"><input type="hidden" name="token" value=")HTML";
    h += escape(token);
    h += R"HTML("><input type="hidden" name="epoch" id="epoch"><label for="ssid">Wi-Fi 2,4 ГГц</label><input id="ssid" name="ssid" maxlength="32" required value=")HTML";
    h += escape(s.ssid);
    h += R"HTML("><label for="password">Пароль Wi-Fi</label><input id="password" name="password" type="password" maxlength="64" autocomplete="new-password"><small>Для прежней сети пустое поле сохраняет пароль.</small><label><input type="checkbox" name="open" value="1"> Сеть без пароля</label><label for="ntp">Сервер точного времени</label><input id="ntp" name="ntp" maxlength="64" required value=")HTML";
    h += escape(s.ntp);
    h += R"HTML("><label for="server">Адрес сервиса распознавания</label><input id="server" name="server" maxlength="128" required placeholder="http://192.168.1.10:8000/v1/audio/transcriptions" value=")HTML";
    h += escape(s.server);
    h += R"HTML("><small>OpenAI-совместимый POST /v1/audio/transcriptions, только http://.</small><label for="timezone">Часовой пояс (POSIX TZ)</label><input id="timezone" name="timezone" maxlength="96" required value=")HTML";
    h += escape(s.timezone);
    h += R"HTML("><small>Например: MSK-3 (Москва), UTC0, CET-1CEST,M3.5.0,M10.5.0/3 (Берлин), EST5EDT,M3.2.0,M11.1.0 (Нью-Йорк).</small><label for="volume">Громкость сигналов (40–100)</label><input id="volume" name="volume" type="number" min="40" max="100" required value=")HTML";
    h += s.volume;
    h += R"HTML("><small>При сохранении передадим текущее время телефона. Распознавание: русский язык.</small><button>Сохранить</button></form><p><a href="/cancel">Вернуться без изменений</a></p><script>document.getElementById('setup').onsubmit=()=>{document.getElementById('epoch').value=Math.floor(Date.now()/1000)};</script></html>)HTML";
    return h;
}
bool store(const Settings &s) {
    Preferences nvs;
    if (!nvs.begin(storageNamespace, false)) return false;
    const bool ok = nvs.putBytes("config", &s, sizeof(s)) == sizeof(s);
    nvs.end(); return ok;
}
// Layout written by firmware up to November 2026; migrated without server and time zone.
struct SettingsV1 {
    uint32_t version;
    char ssid[33];
    char password[65];
    char ntp[65];
    uint8_t volume;
};
template <size_t N> bool terminated(const char (&value)[N]) { return memchr(value, 0, N) != nullptr; }
bool common(const Settings &v) {
    return terminated(v.ssid) && v.ssid[0] && terminated(v.password) && validation::password(v.password) &&
        terminated(v.ntp) && validation::hostname(v.ntp) && v.volume >= 40 && v.volume <= 100;
}
}
bool loadSettings(Settings &s) {
    Preferences nvs;
    if (!nvs.begin(storageNamespace, true)) return false;
    Settings value;
    const size_t size = nvs.getBytesLength("config");
    bool found = false, migrated = false;
    if (size == sizeof(Settings)) {
        found = nvs.getBytes("config", &value, sizeof(value)) == sizeof(value) && value.version == 2;
    } else if (size == sizeof(SettingsV1)) {
        SettingsV1 old;
        found = migrated = nvs.getBytes("config", &old, sizeof(old)) == sizeof(old) && old.version == 1;
        if (found) {
            memcpy(value.ssid, old.ssid, sizeof(old.ssid)); memcpy(value.password, old.password, sizeof(old.password));
            memcpy(value.ntp, old.ntp, sizeof(old.ntp)); value.volume = old.volume;
        }
    }
    nvs.end();
    if (!found || !common(value)) return false;
    if (migrated) { s = value; return false; } // Keep Wi-Fi, ask for the server in the portal.
    if (!terminated(value.server) || !validation::serverUrl(value.server) ||
        !terminated(value.timezone) || !validation::timezone(value.timezone)) return false;
    s = value; return true;
}
void applyTimezone(const Settings &s) {
    setenv("TZ", s.timezone[0] ? s.timezone : "UTC0", 1); tzset();
}
std::string serverHost(const Settings &s) {
    std::string host;
    return validation::serverUrl(s.server, &host) ? host : std::string("сервере");
}
bool setupPortal(Settings &s) {
    WiFi.persistent(false); WiFi.mode(WIFI_AP);
    const IPAddress address(192,168,4,1);
    WiFi.softAPConfig(address, address, IPAddress(255,255,255,0));
    char name[28];
    snprintf(name, sizeof(name), "NOTE4C-VOICE-%04X", static_cast<unsigned>(ESP.getEfuseMac() >> 32) & 0xffff);
    if (!WiFi.softAP(name, "NOTE4CVOICE")) { WiFi.mode(WIFI_OFF); return false; }
    DNSServer dns; WebServer server(80);
    dns.start(53, "*", address);
    char tokenBuffer[17];
    snprintf(tokenBuffer, sizeof(tokenBuffer), "%08lx%08lx", static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
    const String token(tokenBuffer);
    bool finished = false, saved = false;
    server.on("/", HTTP_GET, [&]() {
        server.sendHeader("Cache-Control", "no-store"); server.send(200, "text/html; charset=utf-8", page(s, token));
    });
    server.on("/save", HTTP_POST, [&]() {
        if (server.arg("token") != token) { server.send(403, "text/plain; charset=utf-8", "Откройте страницу настройки заново."); return; }
        const String ssid = server.arg("ssid"), ntp = server.arg("ntp");
        const String url = server.arg("server"), zone = server.arg("timezone");
        String password = server.arg("password");
        if (password.isEmpty() && ssid == s.ssid) password = s.password;
        if (server.arg("open") == "1") password = "";
        const int volume = server.arg("volume").toInt();
        int64_t epoch = 0;
        if (ssid.isEmpty() || ssid.length() > 32 || !validation::password(password.c_str()) ||
            !validation::hostname(ntp.c_str()) || !validation::serverUrl(url.c_str()) ||
            !validation::timezone(zone.c_str()) || volume < 40 || volume > 100 ||
            !validation::epoch(server.arg("epoch").c_str(), epoch)) {
            server.send(400, "text/plain; charset=utf-8", "Проверьте Wi-Fi, пароль, сервер времени, адрес сервиса, часовой пояс, громкость и время телефона."); return;
        }
        Settings updated;
        ssid.toCharArray(updated.ssid, sizeof(updated.ssid));
        password.toCharArray(updated.password, sizeof(updated.password));
        ntp.toCharArray(updated.ntp, sizeof(updated.ntp)); updated.volume = volume;
        url.toCharArray(updated.server, sizeof(updated.server));
        zone.toCharArray(updated.timezone, sizeof(updated.timezone));
        if (!store(updated)) { server.send(500, "text/plain; charset=utf-8", "Не удалось сохранить настройки."); return; }
        s = updated; applyTimezone(s);
        timeval now{static_cast<time_t>(epoch), 0}; settimeofday(&now, nullptr);
        server.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset=utf-8><h1>Настройки сохранены</h1><p>После обновления экрана нажмите круглую кнопку. Говорите после короткого сигнала.</p>");
        saved = finished = true;
    });
    server.on("/cancel", HTTP_GET, [&]() {
        if (!s.ssid[0] || !clock_logic::validTime(time(nullptr))) {
            server.send(409, "text/plain; charset=utf-8", "Сначала сохраните сеть и время телефона."); return;
        }
        server.send(200, "text/plain; charset=utf-8", "Возвращаемся к заметкам."); finished = true;
    });
    server.onNotFound([&]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
    server.begin();
    static Canvas screen;
    const std::string instruction = std::string("Подключитесь к Wi-Fi ") + name +
        ". Пароль: NOTE4CVOICE. Откройте 192.168.4.1 и сохраните настройки.";
    screen.note(instruction, ""); epd::update(screen.data());
    const uint32_t started = millis();
    while (!finished && millis() - started < 600000) { dns.processNextRequest(); server.handleClient(); delay(5); }
    delay(500); server.stop(); dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF);
    return saved;
}
