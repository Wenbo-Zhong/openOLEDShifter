// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_desktop.hpp"
#include "windows_storage.hpp"
#include "windows_window.hpp"

#include <cstdint>
#include <fstream>
#include <shellapi.h>
#include <sstream>

namespace oled::win {
namespace fs = std::filesystem;
namespace {
struct LayerPosition {
    Offset origin;
    LONG width, height;
};

LayerPosition layer_position(HWND layer, HWND parent) {
    RECT bounds{};
    if (!GetWindowRect(layer, &bounds))
        throw std::runtime_error("Cannot get desktop icon bounds");
    POINT point{bounds.left, bounds.top};
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(nullptr, parent, &point, 1) && GetLastError() != ERROR_SUCCESS)
        throw std::runtime_error("Cannot map desktop icon coordinates");
    return {{static_cast<int>(point.x), static_cast<int>(point.y)},
            bounds.right - bounds.left, bounds.bottom - bounds.top};
}

struct IconRecord {
    WindowIdentity id;
    std::uintptr_t parent = 0;
    LONG width = 0, height = 0;
    Offset origin{}, previous{}, next{};
};

IconRecord read_icon_record(const fs::path& path) {
    std::ifstream input(path);
    IconRecord record;
    std::string version;
    if (!(input >> version >> record.id >> record.parent >> record.width >> record.height >>
          record.origin.x >> record.origin.y >> record.previous.x >> record.previous.y >>
          record.next.x >> record.next.y) || version != "icons-1" ||
        !record.id.window || !record.id.pid || !record.parent || record.width <= 0 || record.height <= 0)
        throw std::runtime_error("Invalid desktop icon recovery journal");
    return record;
}

void save_icon_record(const fs::path& path, const IconRecord& record) {
    std::ostringstream stream;
    stream << "icons-1 " << record.id << ' ' << record.parent << ' '
           << record.width << ' ' << record.height << ' ' << record.origin.x << ' ' << record.origin.y << ' '
           << record.previous.x << ' ' << record.previous.y << ' ' << record.next.x << ' ' << record.next.y;
    const auto contents = stream.str();
    atomic_write(path, std::as_bytes(std::span(contents)));
}

bool owns_icon_position(const IconRecord& record, const LayerPosition& current) {
    return current.width == record.width && current.height == record.height &&
           (current.origin == record.next || current.origin == record.previous);
}

struct TaskbarRecord {
    WindowIdentity id;
    UINT original = 0, changed = 0;
};

TaskbarRecord read_taskbar_record(const fs::path& path) {
    std::ifstream input(path);
    TaskbarRecord record;
    std::string version;
    if (!(input >> version >> record.id >> record.original >> record.changed) ||
        version != "taskbar-1" || !record.id.window || !record.id.pid ||
        (record.original & ABS_AUTOHIDE) || record.changed != (record.original | ABS_AUTOHIDE))
        throw std::runtime_error("Invalid taskbar recovery journal");
    return record;
}

class ExplorerTaskbar final : public Taskbar {
    HWND window() const override {
        const auto tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        DWORD shell_pid{}, tray_pid{};
        GetWindowThreadProcessId(GetShellWindow(), &shell_pid);
        if (!tray || !GetWindowThreadProcessId(tray, &tray_pid) || tray_pid != shell_pid)
            return nullptr;
        return tray;
    }

    UINT state(HWND window) const override {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &data));
    }

    void set_state(HWND window, UINT state) const override {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.lParam = state;
        SHAppBarMessage(ABM_SETSTATE, &data);
        if (this->state(window) != state)
            throw std::runtime_error("Explorer did not accept the taskbar setting");
    }
};

struct IconSearch {
    DWORD shell_pid;
    HWND found = nullptr;
};

BOOL CALLBACK find_icon_view(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<IconSearch*>(data);
    DWORD pid{};
    if (!GetWindowThreadProcessId(window, &pid) || pid != search.shell_pid)
        return TRUE;
    const auto host = FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr);
    if (host)
        search.found = FindWindowExW(host, nullptr, L"SysListView32", nullptr);
    return search.found ? FALSE : TRUE;
}
}

HWND desktop_icon_layer() {
    DWORD pid{};
    if (!GetWindowThreadProcessId(GetShellWindow(), &pid))
        return nullptr;
    IconSearch search{pid};
    EnumWindows(find_icon_view, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

void shift_icon_layer(HWND layer, const fs::path& journal, Offset offset) {
    const auto id = identify(layer);
    const auto parent = layer ? GetParent(layer) : nullptr;
    if (!id.window || !parent || !IsWindowVisible(layer) || IsIconic(layer))
        throw std::runtime_error("Desktop icon layer is unavailable");
    const auto position = layer_position(layer, parent);
    const auto parent_id = reinterpret_cast<std::uintptr_t>(parent);
    IconRecord record{id, parent_id, position.width, position.height, position.origin, position.origin, position.origin};
    if (fs::exists(journal)) {
        record = read_icon_record(journal);
        if (record.id != id || record.parent != parent_id || !owns_icon_position(record, position))
            throw std::runtime_error("Desktop icon layer changed outside OpenOLEDShifter");
    }
    const Offset target{record.origin.x + offset.x, record.origin.y + offset.y};
    if (target == position.origin)
        return;
    record.previous = position.origin;
    record.next = target;
    save_icon_record(journal, record);
    if (identify(layer) != id || GetParent(layer) != parent ||
        layer_position(layer, parent).origin != position.origin)
        throw std::runtime_error("Desktop icon layer changed while preparing shift");
    move_window(layer, target);
}

void restore_icon_layer(HWND layer, const fs::path& journal) {
    if (!fs::exists(journal))
        return;
    const auto record = read_icon_record(journal);
    const auto recorded = reinterpret_cast<HWND>(record.id.window);
    if (!layer && identify(recorded) == record.id)
        throw std::runtime_error("Desktop icon layer is temporarily unavailable");
    if (identify(layer) == record.id) {
        const auto parent = GetParent(layer);
        if (parent && reinterpret_cast<std::uintptr_t>(parent) == record.parent &&
            owns_icon_position(record, layer_position(layer, parent)))
            move_window(layer, record.origin);
    }
    fs::remove(journal);
}

const Taskbar& explorer_taskbar() {
    static ExplorerTaskbar instance;
    return instance;
}

void hide_taskbar(const Taskbar& taskbar, const fs::path& journal) {
    if (fs::exists(journal))
        return;
    const auto tray = taskbar.window();
    const auto id = identify(tray);
    if (!id.window)
        throw std::runtime_error("Windows taskbar is unavailable");
    const auto original = taskbar.state(tray);
    if (original & ABS_AUTOHIDE)
        return;
    const auto changed = original | ABS_AUTOHIDE;
    std::ostringstream stream;
    stream << "taskbar-1 " << id << ' ' << original << ' ' << changed;
    const auto contents = stream.str();
    atomic_write(journal, std::as_bytes(std::span(contents)));
    if (identify(taskbar.window()) != id || taskbar.state(tray) != original)
        throw std::runtime_error("Taskbar changed while preparing auto-hide");
    taskbar.set_state(tray, changed);
}

void restore_taskbar(const Taskbar& taskbar, const fs::path& journal) {
    if (!fs::exists(journal))
        return;
    const auto record = read_taskbar_record(journal);
    const auto tray = taskbar.window();
    const auto recorded = reinterpret_cast<HWND>(record.id.window);
    if (!tray && identify(recorded) == record.id)
        throw std::runtime_error("Windows taskbar is temporarily unavailable");
    if (identify(tray) == record.id && taskbar.state(tray) == record.changed)
        taskbar.set_state(tray, record.original);
    fs::remove(journal);
}
}
