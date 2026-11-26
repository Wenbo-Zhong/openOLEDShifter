// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "core/shift_core.hpp"
#include <cstdint>
#include <istream>
#include <ostream>
#include <system_error>
#include <windows.h>

namespace oled::win {
// 句柄和 PID 都可能复用；同时校验进程创建时间，避免恢复记录命中重启后的其他进程。
struct WindowIdentity {
    std::uintptr_t window = 0;
    DWORD pid = 0;
    std::uint64_t creation = 0;
    bool operator==(const WindowIdentity&) const = default;
};

inline WindowIdentity identify(HWND window) {
    DWORD pid{};
    if (!window || !GetWindowThreadProcessId(window, &pid))
        return {};
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        const auto error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER)
            return {};
        throw std::system_error(error, std::system_category(), "Identify desktop process");
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    const auto success = GetProcessTimes(process, &created, &exited, &kernel, &user);
    const auto error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    if (!success)
        throw std::system_error(error, std::system_category(), "Read process start time");
    return {reinterpret_cast<std::uintptr_t>(window), pid,
            (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime};
}

inline std::istream& operator>>(std::istream& in, WindowIdentity& id) {
    return in >> id.window >> id.pid >> id.creation;
}

inline std::ostream& operator<<(std::ostream& out, const WindowIdentity& id) {
    return out << id.window << ' ' << id.pid << ' ' << id.creation;
}

inline void move_window(HWND window, Offset position) {
    if (!SetWindowPos(window, nullptr, position.x, position.y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::system_error(GetLastError(), std::system_category(), "Move desktop window");
}
}
