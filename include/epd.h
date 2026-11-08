// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include "panel.h"

namespace epd {
void begin();
bool update(const uint8_t *framebuffer);
bool updateWindow(const uint8_t *framebuffer, panel::Rect window);
}

