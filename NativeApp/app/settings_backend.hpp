// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "core/shift_core.hpp"

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

// 只报告后端实际实现的能力；界面据此创建控件，不能仅增加开关就宣称支持新功能。
struct DesktopFeatures {
    bool wallpaper_toggle = false;
    bool desktop_icons = false;
    bool taskbar_auto_hide = false;
    bool wallpaper_monitor_selection = false;
    bool application_windows = false;
    bool immediate_startup = false;
    bool fullscreen_pause = false;
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

enum class Command { Read, Apply, Pause, Shift, SetStartup, Toggle, Setup };

class Backend {
public:
    virtual ~Backend() = default;
    virtual const char* description() const = 0;
    virtual const char* failure_hint() const = 0;
    virtual const char* setup_label() const { return nullptr; }
    virtual DesktopFeatures features() const { return {}; }
    virtual const char* login_label() const { return "Start shifting at login"; }
    virtual bool supports_login_startup() const { return true; }
    virtual bool supports_manual_shift() const { return true; }
    // true 时命令队列在工作线程调用后端；false 时与 UI 和平台事件在主线程串行执行。
    virtual bool use_worker() const { return true; }
    virtual State call(Command command, const Settings& settings) = 0;
};

Backend& desktop_backend();
}
