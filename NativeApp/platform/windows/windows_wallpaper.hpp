// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "core/shift_core.hpp"
#include <filesystem>
#include <span>
#include <windows.h>
#include <shobjidl.h>

namespace oled::win {
class WallpaperSession {
public:
    WallpaperSession();
    ~WallpaperSession();
    WallpaperSession(const WallpaperSession&) = delete;
    WallpaperSession& operator=(const WallpaperSession&) = delete;

    IDesktopWallpaper& api() const { return *api_; }

private:
    IDesktopWallpaper* api_ = nullptr;
};

// Write a rendered PNG without changing any desktop state.
void render_wallpaper(const std::filesystem::path& source, const std::filesystem::path& output,
                      int width, int height, DESKTOP_WALLPAPER_POSITION mode, COLORREF color,
                      Offset offset);

// Each monitor has an independent journal file. Keep the file and cache in the app's data directory.
void shift_wallpaper(IDesktopWallpaper& api, const std::wstring& monitor,
                     const std::filesystem::path& journal, Offset offset);
// False means the monitor is disconnected and its recovery file remains pending.
bool restore_wallpaper(IDesktopWallpaper& api, const std::filesystem::path& journal,
                       std::span<const std::wstring> active_monitors);
}
