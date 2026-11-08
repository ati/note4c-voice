// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio.h"
#include <string>
// url must pass validation::serverUrl; host is used in the "unavailable" message.
std::string transcribe(const audio::Recording &recording, const char *url, const std::string &host,
                       bool *recognized = nullptr);
