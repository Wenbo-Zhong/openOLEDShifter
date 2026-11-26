// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "app/settings_backend.hpp"
#include <filesystem>

namespace oled::win {
struct WindowsConfig {
    Settings settings;
    std::vector<std::wstring> window_processes;
};

WindowsConfig load_config(const std::filesystem::path& file);
void save_config(const std::filesystem::path& file, const Settings& settings);
}
