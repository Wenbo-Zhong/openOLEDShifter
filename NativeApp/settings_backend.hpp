// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "shift_core.hpp"

namespace oled {
struct Settings {
    int interval = default_interval;
    int radius = default_radius;
    bool enabled = false;
    bool login = false;
    bool wallpaper = true;
    bool desktop_icons = false;
    bool taskbar_auto_hide = false;
    std::string wallpaper_display = "primary";
    std::string window_processes{};
    bool shift_fullscreen_shell = false;
    bool pause_fullscreen = true;
};

struct DesktopFeatures {
    bool wallpaper_toggle = false;
    bool desktop_icons = false;
    bool taskbar_auto_hide = false;
    bool wallpaper_monitor_selection = false;
    bool application_windows = false;
    bool immediate_startup = false;
};

struct DisplayChoice {
    std::string id;
    std::string name;
    bool operator==(const DisplayChoice&) const = default;
};

struct State {
    Settings settings;
    std::string status;
    std::vector<DisplayChoice> wallpaper_displays;
};

enum class Command { Read, Apply, Pause, Shift, SetStartup };

class Backend {
public:
    virtual ~Backend() = default;
    virtual const char* description() const = 0;
    virtual const char* failure_hint() const = 0;
    virtual DesktopFeatures features() const { return {}; }
    virtual bool use_worker() const { return true; }
    virtual State call(Command command, const Settings& settings) = 0;
};

Backend& desktop_backend();
}
