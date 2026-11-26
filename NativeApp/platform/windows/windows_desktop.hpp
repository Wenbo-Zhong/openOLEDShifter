// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "core/shift_core.hpp"
#include <filesystem>
#include <windows.h>

namespace oled::win {
HWND desktop_icon_layer();
void shift_icon_layer(HWND layer, const std::filesystem::path& journal, Offset offset);
void restore_icon_layer(HWND layer, const std::filesystem::path& journal);

class Taskbar {
public:
    virtual ~Taskbar() = default;
    virtual HWND window() const = 0;
    virtual UINT state(HWND window) const = 0;
    virtual void set_state(HWND window, UINT state) const = 0;
};

const Taskbar& explorer_taskbar();
void hide_taskbar(const Taskbar& taskbar, const std::filesystem::path& journal);
void restore_taskbar(const Taskbar& taskbar, const std::filesystem::path& journal);
}
