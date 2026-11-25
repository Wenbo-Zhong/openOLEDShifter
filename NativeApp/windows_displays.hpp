// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "windows_wallpaper.hpp"

namespace oled::win {
struct WallpaperDisplay {
    std::wstring device_path;
    std::wstring screen_name;
    RECT bounds{};
    bool primary = false;

    std::string key() const;
    bool operator==(const WallpaperDisplay& other) const;
};

std::vector<WallpaperDisplay> active_wallpaper_displays(IDesktopWallpaper& api);
bool valid_display_selection(std::string_view id);
bool selects_display(std::string_view selection, const WallpaperDisplay& display);
std::filesystem::path wallpaper_journal(const std::filesystem::path& directory, const WallpaperDisplay& display);
std::vector<std::filesystem::path> wallpaper_journals(const std::filesystem::path& directory);
}
