// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "shift_core.hpp"
#include <filesystem>
#include <windows.h>

namespace oled::win {
std::wstring from_utf8(std::string_view text);
std::string to_utf8(std::wstring_view text);
std::vector<std::wstring> parse_window_processes(std::wstring_view names);
bool fullscreen_window(HWND window);
bool known_fullscreen_shell(HWND window);
std::vector<HWND> allowed_app_windows(const std::vector<std::wstring>& processes, bool retail_demo);
std::filesystem::path app_window_journal(const std::filesystem::path& directory, HWND window);
std::vector<std::filesystem::path> app_window_journals(const std::filesystem::path& directory);
HWND journaled_app_window(const std::filesystem::path& journal);
void shift_app_window(HWND window, const std::filesystem::path& journal, Offset offset, bool fullscreen_shell);
void restore_app_window(const std::filesystem::path& journal);
}
