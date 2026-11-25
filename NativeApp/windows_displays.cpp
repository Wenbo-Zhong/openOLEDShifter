// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_displays.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <tuple>

namespace oled::win {
namespace fs = std::filesystem;
namespace {
bool is_key(std::string_view id) {
    return id.size() == 16 && std::ranges::all_of(id, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

struct Screen {
    std::wstring name;
    RECT bounds;
    bool primary;
};

BOOL CALLBACK collect_screen(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto& screens = *reinterpret_cast<std::vector<Screen>*>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info))
        screens.push_back({info.szDevice, info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
    return TRUE;
}
}

std::string WallpaperDisplay::key() const {
    // COM paths identify physical monitors. Keep filenames short and independent of enumeration order.
    std::uint64_t hash = 14695981039346656037ULL;
    for (auto ch : device_path) {
        if (ch >= L'A' && ch <= L'Z')
            ch += L'a' - L'A';
        hash = (hash ^ static_cast<std::uint16_t>(ch)) * 1099511628211ULL;
    }
    char out[17]{};
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}

bool WallpaperDisplay::operator==(const WallpaperDisplay& other) const {
    return device_path == other.device_path && screen_name == other.screen_name &&
           EqualRect(&bounds, &other.bounds) && primary == other.primary;
}

std::vector<WallpaperDisplay> active_wallpaper_displays(IDesktopWallpaper& api) {
    std::vector<Screen> screens;
    if (!EnumDisplayMonitors(nullptr, nullptr, collect_screen, reinterpret_cast<LPARAM>(&screens)))
        throw std::runtime_error("Cannot enumerate Windows displays");
    UINT count{};
    const auto result = api.GetMonitorDevicePathCount(&count);
    if (FAILED(result))
        throw std::system_error(static_cast<int>(result), std::system_category(), "Read wallpaper display count");
    std::vector<WallpaperDisplay> displays;
    for (UINT index = 0; index < count; ++index) {
        PWSTR value = nullptr;
        const auto result = api.GetMonitorDevicePathAt(index, &value);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(value, CoTaskMemFree);
        RECT bounds{};
        if (FAILED(result) || !value || api.GetMonitorRECT(value, &bounds) != S_OK)
            continue;
        const auto screen = std::ranges::find_if(screens, [&](const auto& active) {
            return EqualRect(&active.bounds, &bounds);
        });
        if (screen != screens.end())
            displays.push_back({value, screen->name, bounds, screen->primary});
    }
    std::ranges::sort(displays, [](const auto& a, const auto& b) {
        return std::tuple(!a.primary, a.screen_name, a.device_path) <
               std::tuple(!b.primary, b.screen_name, b.device_path);
    });
    bool primary_seen = false;
    for (auto& display : displays) {
        if (display.primary) {
            display.primary = !primary_seen;
            primary_seen = true;
        }
    }
    return displays;
}

bool valid_display_selection(std::string_view id) {
    return id == "primary" || id == "all" || is_key(id);
}

bool selects_display(std::string_view selection, const WallpaperDisplay& display) {
    return selection == "all" || (selection == "primary" ? display.primary : selection == display.key());
}

fs::path wallpaper_journal(const fs::path& directory, const WallpaperDisplay& display) {
    return directory / ("wallpaper-" + display.key() + ".recovery");
}

std::vector<fs::path> wallpaper_journals(const fs::path& directory) {
    std::vector<fs::path> result;
    constexpr std::wstring_view prefix = L"wallpaper-", suffix = L".recovery";
    const auto legacy = directory / L"wallpaper.recovery";
    // Handle the previous preview's journal before any new per-display journals.
    if (fs::exists(legacy))
        result.push_back(legacy);
    for (const auto& entry : fs::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        if (!entry.is_regular_file() || name.size() != prefix.size() + 16 + suffix.size() ||
            !name.starts_with(prefix) || !name.ends_with(suffix))
            continue;
        const auto key = std::wstring_view(name).substr(prefix.size(), 16);
        if (std::ranges::all_of(key, [](wchar_t c) {
            return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
        }))
            result.push_back(entry.path());
    }
    return result;
}
}
