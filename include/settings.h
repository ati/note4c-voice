// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
struct Settings {
    uint32_t version = 2;
    char ssid[33] = "";
    char password[65] = "";
    char ntp[65] = "pool.ntp.org";
    uint8_t volume = 85;
    char server[129] = ""; // http://host[:port]/path of an OpenAI-compatible transcription endpoint.
    char timezone[97] = "MSK-3"; // POSIX TZ string.
};
// Returns false when settings are missing or incomplete; fields that could be
// read (including a version 1 migration) are still copied for the portal.
bool loadSettings(Settings &settings);
bool setupPortal(Settings &settings);
void applyTimezone(const Settings &settings);
std::string serverHost(const Settings &settings);
