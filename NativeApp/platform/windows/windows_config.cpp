// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_config.hpp"
#include "windows_app_windows.hpp"
#include "windows_displays.hpp"
#include "windows_storage.hpp"

#include <algorithm>
#include <memory>
#include <shlobj.h>
#include <sstream>
#include <utility>

namespace oled::win {
std::filesystem::path data_root() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(path, CoTaskMemFree);
    if (FAILED(result) || !path)
        throw std::runtime_error("Cannot locate LocalAppData");
    return path;
}

WindowsConfig load_config(const std::filesystem::path& file) {
    std::filesystem::create_directories(file.parent_path());
    WindowsConfig loaded;
    auto& settings = loaded.settings;
    const auto read = [&file](const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(L"Shifter", key, fallback, file.c_str()));
    };
    settings.interval = std::clamp(read(L"IntervalSeconds", default_interval), min_interval, max_interval);
    settings.radius = std::clamp(read(L"Radius", default_radius), min_radius, max_radius);
    settings.enabled = read(L"Enabled", 0) == 1;
    settings.wallpaper = read(L"Wallpaper", 1) == 1;
    settings.desktop_icons = read(L"DesktopIcons", 0) == 1;
    settings.taskbar_auto_hide = read(L"TaskbarAutoHide", 0) == 1;
    settings.pause_fullscreen = read(L"PauseFullscreen", 1) == 1;
    settings.shift_fullscreen_shell = read(L"ShiftFullscreenShell", 0) == 1;
    wchar_t names[2049]{};
    GetPrivateProfileStringW(L"Shifter", L"WindowProcesses", L"", names, 2049, file.c_str());
    loaded.window_processes = parse_window_processes(names);
    settings.window_processes = to_utf8(join_processes(loaded.window_processes));
    // Read the ASCII key through the wide profile API so non-ASCII profile paths work.
    wchar_t wide_selection[64]{};
    GetPrivateProfileStringW(L"Shifter", L"WallpaperDisplay", L"primary", wide_selection, 64, file.c_str());
    std::string selected;
    for (const wchar_t* ch = wide_selection; *ch; ++ch) {
        if (*ch > 127) {
            selected.clear();
            break;
        }
        selected.push_back(static_cast<char>(*ch));
    }
    if (valid_display_selection(selected))
        settings.wallpaper_display = std::move(selected);
    return loaded;
}

void save_config(const std::filesystem::path& file, const Settings& settings) {
    std::wostringstream output;
    output << L"\ufeff[Shifter]\r\nIntervalSeconds=" << settings.interval << L"\r\nRadius="
           << settings.radius << L"\r\nEnabled=" << settings.enabled
           << L"\r\nWallpaper=" << settings.wallpaper << L"\r\nDesktopIcons=" << settings.desktop_icons
           << L"\r\nTaskbarAutoHide=" << settings.taskbar_auto_hide
           << L"\r\nWallpaperDisplay=" << from_utf8(settings.wallpaper_display)
           << L"\r\nPauseFullscreen=" << settings.pause_fullscreen
           << L"\r\nShiftFullscreenShell=" << settings.shift_fullscreen_shell
           << L"\r\nWindowProcesses=" << from_utf8(settings.window_processes) << L"\r\n";
    const auto contents = output.str();
    atomic_write(file, std::as_bytes(std::span(contents)));
}
}
