// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

// Source: trmnl-firmware origin/zectrix, 76f89b09dec420c73a970a7c5bb2fcc61009eba1.
namespace board {
constexpr int epdPower = 6;
constexpr int epdBusy = 8;
constexpr int epdReset = 9;
constexpr int epdDc = 10;
constexpr int epdCs = 11;
constexpr int epdSck = 12;
constexpr int epdMosi = 13;
constexpr int batteryHold = 17;
constexpr int button = 0;
constexpr int settingsButton = 39;
constexpr int downButton = 18; // Zectrix shared board: DOWN / power, active LOW.
constexpr int led = 3; // Active LOW.
constexpr int audioPower = 42;
constexpr int amplifier = 46;
constexpr int audioMclk = 14;
constexpr int audioBclk = 15;
constexpr int audioWs = 38;
constexpr int audioTx = 45;
constexpr int audioRx = 16;
constexpr int i2cSda = 47;
constexpr int i2cScl = 48;

void begin();
bool bootButtonPressed();
bool bootUpPressed();
bool bootDownPressed();
uint32_t bootPressedAt();
void prepareSleep();
} // namespace board
