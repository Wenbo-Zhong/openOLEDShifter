// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "app/settings_backend.hpp"

namespace oled::linux_desktop {
enum class Desktop { Gnome, Plasma };

bool plugin_installed(const char* directory, const char* id);
bool autostart_enabled(Desktop desktop);
void save_autostart(Desktop desktop, bool enabled);

Backend& gnome_backend();
Backend& plasma_backend();
}
