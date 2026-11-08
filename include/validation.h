// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>
#include "clock_logic.h"

namespace validation {
inline bool number(const char *&cursor, int minimum, int maximum) {
    if (*cursor < '0' || *cursor > '9') return false;
    int value = 0;
    do {
        value = value * 10 + *cursor++ - '0';
        if (value > maximum) return false;
    } while (*cursor >= '0' && *cursor <= '9');
    return value >= minimum;
}

inline bool zoneName(const char *&cursor) {
    if (*cursor == '<') {
        ++cursor;
        int letters = 0;
        while (std::isalnum(static_cast<unsigned char>(*cursor)) || *cursor == '+' || *cursor == '-') {
            ++letters;
            ++cursor;
        }
        if (letters < 3 || *cursor != '>') return false;
        ++cursor;
    } else {
        int letters = 0;
        while (std::isalpha(static_cast<unsigned char>(*cursor))) { ++cursor; ++letters; }
        if (letters < 3) return false;
    }
    return true;
}

inline bool offset(const char *&cursor, int maximumHour) {
    if (*cursor == '+' || *cursor == '-') ++cursor;
    if (!number(cursor, 0, maximumHour)) return false;
    if (*cursor == ':') {
        ++cursor;
        if (!number(cursor, 0, 59)) return false;
        if (*cursor == ':') {
            ++cursor;
            if (!number(cursor, 0, 59)) return false;
        }
    }
    return true;
}

inline bool transition(const char *&cursor) {
    if (*cursor == 'M') {
        ++cursor;
        if (!number(cursor, 1, 12) || *cursor != '.') return false;
        ++cursor;
        if (!number(cursor, 1, 5) || *cursor != '.') return false;
        ++cursor;
        if (!number(cursor, 0, 6)) return false;
    } else if (*cursor == 'J') {
        ++cursor;
        if (!number(cursor, 1, 365)) return false;
    } else if (!number(cursor, 0, 365)) {
        return false;
    }
    if (*cursor == '/') {
        ++cursor;
        if (!offset(cursor, 167)) return false;
    }
    return true;
}

inline bool timezone(const char *value) {
    const size_t size = std::strlen(value);
    if (size < 4 || size > 96) return false;
    // POSIX TZ, not an IANA database name such as Europe/Moscow.
    const char *cursor = value;
    if (!zoneName(cursor) || !offset(cursor, 24)) return false;
    if (*cursor == '\0') return true;
    if (!zoneName(cursor)) return false;
    if (*cursor && *cursor != ',' && !offset(cursor, 24)) return false;
    if (*cursor == '\0') return true;
    if (*cursor != ',') return false;
    ++cursor;
    if (!transition(cursor) || *cursor != ',') return false;
    ++cursor;
    return transition(cursor) && *cursor == '\0';
}

inline bool hostname(const char *value) {
    const size_t length = std::strlen(value);
    if (length == 0 || length > 64) return false;
    // Labels are non-empty and do not start or end with '-'.
    char previous = '.';
    for (const char *p = value; *p; previous = *p++) {
        if (!std::isalnum(static_cast<unsigned char>(*p)) && *p != '-' && *p != '.') return false;
        if ((*p == '.' || *p == '-') && previous == '.') return false;
        if (*p == '.' && previous == '-') return false;
    }
    return previous != '.' && previous != '-';
}

// Plain HTTP URL: http://host[:port]/path. Host is a DNS name or IPv4 address.
inline bool serverUrl(const char *value, std::string *host = nullptr) {
    const size_t length = std::strlen(value);
    if (length > 128 || std::strncmp(value, "http://", 7) != 0) return false;
    const char *start = value + 7, *end = start;
    while (*end && *end != ':' && *end != '/') ++end;
    const std::string name(start, end);
    if (!hostname(name.c_str())) return false;
    const char *cursor = end;
    if (*cursor == ':') {
        ++cursor;
        if (!number(cursor, 1, 65535)) return false;
    }
    if (*cursor != '/') return false;
    for (; *cursor; ++cursor)
        if (!std::isalnum(static_cast<unsigned char>(*cursor)) && !std::strchr("/-._~%?=&+", *cursor)) return false;
    if (host) *host = name;
    return true;
}

inline bool password(const char *value) {
    const size_t length = std::strlen(value);
    if (length == 0) return true;
    if (length < 8 || length > 64) return false;
    if (length == 64)
        for (const char *p = value; *p; ++p)
            if (!std::isxdigit(static_cast<unsigned char>(*p))) return false;
    return true;
}

inline bool epoch(const char *value, int64_t &result) {
    if (std::strlen(value) != 10) return false;
    for (const char *p = value; *p; ++p)
        if (*p < '0' || *p > '9') return false;
    result = std::strtoll(value, nullptr, 10);
    return clock_logic::validTime(result);
}
}
