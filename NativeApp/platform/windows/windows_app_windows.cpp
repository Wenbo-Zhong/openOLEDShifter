// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_app_windows.hpp"
#include "windows_storage.hpp"
#include "windows_window.hpp"

#include <algorithm>
#include <appmodel.h>
#include <dwmapi.h>
#include <exception>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace oled::win {
namespace fs = std::filesystem;
std::wstring join_processes(const std::vector<std::wstring>& processes) {
    std::wstring names;
    for (const auto& process : processes) {
        if (!names.empty())
            names += L", ";
        names += process;
    }
    return names;
}

namespace {
constexpr DWORD open_query = PROCESS_QUERY_LIMITED_INFORMATION;
constexpr std::wstring_view prefix = L"app-window-", suffix = L".recovery";

std::wstring class_name(HWND window) {
    wchar_t name[256]{};
    return std::wstring(name, GetClassNameW(window, name, 256));
}

bool cloaked(HWND window) {
    DWORD value{};
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &value, sizeof(value))) && value != 0;
}

bool visible_top_level(HWND window) {
    if (!window || !IsWindowVisible(window) || IsIconic(window) || cloaked(window) ||
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD))
        return false;
    const auto name = class_name(window);
    return name != L"Progman" && name != L"WorkerW" &&
           name != L"Shell_TrayWnd" && name != L"Shell_SecondaryTrayWnd";
}

DWORD window_pid(HWND window) {
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    return pid;
}

std::wstring process_image(DWORD pid) {
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const auto process = OpenProcess(open_query, FALSE, pid);
    if (!process)
        return {};
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!ok)
        return {};
    path.resize(length);
    return fs::path(path).filename().wstring();
}

struct Selection {
    const std::vector<std::wstring>& processes;
    bool retail_demo;
    std::unordered_map<DWORD, std::wstring> images;
    std::vector<HWND> windows;
    std::exception_ptr failure;
};

BOOL CALLBACK collect(HWND window, LPARAM context) noexcept {
    auto& selected = *reinterpret_cast<Selection*>(context);
    try {
        const auto pid = window_pid(window);
        if (!pid || pid == GetCurrentProcessId() || IsZoomed(window) || !visible_top_level(window))
            return TRUE;
        bool match = false;
        if (!selected.processes.empty()) {
            if (!selected.images.contains(pid))
                selected.images.emplace(pid, process_image(pid));
            const auto& image = selected.images.at(pid);
            match = std::ranges::any_of(selected.processes, [&](const auto& name) {
                return _wcsicmp(name.c_str(), image.c_str()) == 0;
            });
        }
        if (!match && selected.retail_demo && class_name(window) == L"ApplicationFrameWindow") {
            wchar_t title[128]{};
            GetWindowTextW(window, title, 128);
            match = std::wstring_view(title) == L"Microsoft Retail Demo" ||
                    std::wstring_view(title) == L"Intelligent Point of Sale";
        }
        if (match)
            selected.windows.push_back(window);
        return TRUE;
    } catch (...) {
        // 异常不能穿过 Win32 回调边界；交给枚举调用方在返回 C++ 后重新抛出。
        selected.failure = std::current_exception();
        return FALSE;
    }
}

struct Frame {
    Offset location;
    LONG width, height;
};

Frame frame(RECT rect) {
    return {{static_cast<int>(rect.left), static_cast<int>(rect.top)},
            rect.right - rect.left, rect.bottom - rect.top};
}

Frame bounds(HWND window) {
    RECT rect{};
    if (!GetWindowRect(window, &rect))
        throw std::runtime_error("Cannot read application window bounds");
    return frame(rect);
}

WINDOWPLACEMENT placement(HWND window) {
    WINDOWPLACEMENT value{};
    value.length = sizeof(value);
    if (!GetWindowPlacement(window, &value))
        throw std::runtime_error("Cannot read application window placement");
    return value;
}

struct Record {
    WindowIdentity id;
    ULONG_PTR class_atom = 0;
    bool fullscreen = false;
    Frame original{};
    Frame normal{};
    Offset previous{}, next{}, previous_normal{}, next_normal{};
};

Record load(const fs::path& journal) {
    std::ifstream input(journal);
    Record record;
    std::string version;
    auto& [id, atom, fullscreen, original, normal, previous, next, prev_normal, next_normal] = record;
    if (!(input >> version >> id >> atom >> fullscreen >>
          original.location.x >> original.location.y >> original.width >> original.height >>
          normal.location.x >> normal.location.y >> normal.width >> normal.height >>
          previous.x >> previous.y >> next.x >> next.y >>
          prev_normal.x >> prev_normal.y >> next_normal.x >> next_normal.y) ||
        version != "app-window-1" || !id.window || !id.pid || !atom ||
        original.width <= 0 || original.height <= 0 || normal.width <= 0 || normal.height <= 0)
        throw std::runtime_error("Invalid application window recovery journal");
    return record;
}

void save(const fs::path& journal, const Record& record) {
    const auto& [id, atom, fullscreen, original, normal, previous, next, prev_normal, next_normal] = record;
    std::ostringstream output;
    output << "app-window-1 " << id << ' ' << atom << ' ' << fullscreen << ' '
           << original.location.x << ' ' << original.location.y << ' ' << original.width << ' ' << original.height << ' '
           << normal.location.x << ' ' << normal.location.y << ' ' << normal.width << ' ' << normal.height << ' '
           << previous.x << ' ' << previous.y << ' ' << next.x << ' ' << next.y << ' '
           << prev_normal.x << ' ' << prev_normal.y << ' ' << next_normal.x << ' ' << next_normal.y;
    const auto contents = output.str();
    atomic_write(journal, std::as_bytes(std::span(contents)));
}

bool same_owner(HWND window, const Record& record) {
    return identify(window) == record.id && GetClassLongPtrW(window, GCW_ATOM) == record.class_atom;
}

bool same_size(Frame a, Frame b) {
    return a.width == b.width && a.height == b.height;
}

bool owns_position(const Record& record, Frame current) {
    // 同时核对尺寸和日志中的移动前/后位置，用户重新排布窗口后不再拥有恢复权。
    return same_size(record.original, current) &&
           (current.location == record.previous || current.location == record.next);
}

Offset target_position(HWND window, Frame base, Offset shift, bool fullscreen) {
    Offset target{base.location.x + shift.x, base.location.y + shift.y};
    if (fullscreen)
        return target;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        throw std::runtime_error("Cannot locate the application window display");
    constexpr LONG margin = 8;
    const auto& area = monitor.rcWork;
    const auto constrain = [](int base, int next, LONG lower, LONG upper) {
        return static_cast<int>(std::clamp<LONG>(next, std::min<LONG>(lower, base), std::max<LONG>(upper, base)));
    };
    target.x = constrain(base.location.x, target.x, area.left - margin, area.right - base.width + margin);
    target.y = constrain(base.location.y, target.y, area.top - margin, area.bottom - base.height + margin);
    return target;
}
}

std::wstring from_utf8(std::string_view text) {
    if (text.empty())
        return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size)
        throw std::invalid_argument("Invalid UTF-8 text");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string to_utf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (!size)
        throw std::invalid_argument("Invalid Windows text");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::vector<std::wstring> parse_window_processes(std::wstring_view text) {
    std::vector<std::wstring> result;
    constexpr std::wstring_view separator = L",;\r\n";
    constexpr std::wstring_view whitespace = L" \t";
    while (!text.empty()) {
        const auto end = text.find_first_of(separator);
        auto item = text.substr(0, end);
        const auto first = item.find_first_not_of(whitespace);
        if (first != std::wstring_view::npos) {
            item = item.substr(first, item.find_last_not_of(whitespace) - first + 1);
            const auto slash = item.find_last_of(L"/\\");
            if (slash != std::wstring_view::npos)
                item.remove_prefix(slash + 1);
            if (!item.empty()) {
                auto name = std::wstring(item);
                if (name.size() < 4 || _wcsicmp(name.c_str() + name.size() - 4, L".exe"))
                    name += L".exe";
                if (std::ranges::none_of(result, [&](const auto& saved) { return !_wcsicmp(saved.c_str(), name.c_str()); }))
                    result.push_back(std::move(name));
            }
        }
        text = end == std::wstring_view::npos ? std::wstring_view{} : text.substr(end + 1);
    }
    return result;
}

bool fullscreen_window(HWND window) {
    if (!visible_top_level(window))
        return false;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    RECT bounds{};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &monitor) ||
        !GetWindowRect(window, &bounds))
        return false;
    const auto& screen = monitor.rcMonitor;
    return bounds.left <= screen.left + 2 && bounds.top <= screen.top + 2 &&
           bounds.right >= screen.right - 2 && bounds.bottom >= screen.bottom - 2;
}

bool known_fullscreen_shell(HWND window) {
    const auto name = class_name(window);
    const auto pid = window_pid(window);
    if (!pid)
        return false;
    if (name == L"XamlExplorerHostIslandWindow")
        return !_wcsicmp(process_image(pid).c_str(), L"explorer.exe");
    if (name != L"ApplicationFrameWindow")
        return false;
    const auto process = OpenProcess(open_query, FALSE, pid);
    if (!process)
        return false;
    wchar_t id[512]{};
    UINT32 length = 512;
    const auto result = GetApplicationUserModelId(process, &length, id);
    CloseHandle(process);
    if (result != ERROR_SUCCESS)
        return false;
    const std::wstring_view value(id);
    return value == L"Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App" ||
           value == L"Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.AppL" ||
           value == L"B9ECED6F.ArmouryCrateSE_qmba6cd70vzyy!App";
}

std::vector<HWND> allowed_app_windows(const std::vector<std::wstring>& processes, bool retail_demo) {
    if (processes.empty() && !retail_demo)
        return {};
    Selection selected{processes, retail_demo, {}, {}, {}};
    if (!EnumWindows(collect, reinterpret_cast<LPARAM>(&selected))) {
        if (selected.failure)
            std::rethrow_exception(selected.failure);
        throw std::runtime_error("Cannot enumerate application windows");
    }
    return std::move(selected.windows);
}

fs::path app_window_journal(const fs::path& directory, HWND window) {
    return directory / (std::wstring(prefix) + std::to_wstring(reinterpret_cast<std::uintptr_t>(window)) + std::wstring(suffix));
}

std::vector<fs::path> app_window_journals(const fs::path& directory) {
    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(directory)) {
        const auto name = entry.path().filename().wstring();
        if (name.size() <= prefix.size() + suffix.size() ||
            !name.starts_with(prefix) || !name.ends_with(suffix))
            continue;
        const auto id = std::wstring_view(name).substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        if (std::ranges::all_of(id, [](wchar_t c) { return c >= L'0' && c <= L'9'; }) && entry.is_regular_file())
            result.push_back(entry.path());
    }
    return result;
}

HWND journaled_app_window(const fs::path& journal) {
    return fs::exists(journal) ? reinterpret_cast<HWND>(load(journal).id.window) : nullptr;
}

void shift_app_window(HWND window, const fs::path& journal, Offset offset, bool fullscreen_shell) {
    if (!visible_top_level(window) || (!fullscreen_shell && IsZoomed(window)))
        return;
    GUITHREADINFO thread{};
    thread.cbSize = sizeof(thread);
    const auto thread_id = GetWindowThreadProcessId(window, nullptr);
    if (!thread_id || (GetGUIThreadInfo(thread_id, &thread) && (thread.flags & GUI_INMOVESIZE)))
        return;
    const auto current = bounds(window);
    const auto normal = frame(placement(window).rcNormalPosition);
    const auto id = identify(window);
    const auto atom = GetClassLongPtrW(window, GCW_ATOM);
    if (!id.window || !atom || normal.width <= 0 || normal.height <= 0 || current.width <= 0 || current.height <= 0)
        return;
    Record record{id, atom, fullscreen_shell, current, normal, current.location, current.location, normal.location, normal.location};
    if (fs::exists(journal)) {
        const auto previous = load(journal);
        if (previous.id == id && previous.class_atom == atom) {
            if (!owns_position(previous, current))
                return; // The user or application chose a new position.
            record = previous;
        } else {
            restore_app_window(journal);
        }
    }
    const auto target = target_position(window, record.original, offset, fullscreen_shell);
    if (target == current.location)
        return;
    record.previous = current.location;
    record.next = target;
    record.previous_normal = normal.location;
    record.next_normal = {normal.location.x + target.x - current.location.x, normal.location.y + target.y - current.location.y};
    save(journal, record);
    // 记录提交后再核对窗口身份和几何信息，避免覆盖写日志期间发生的拖动或窗口重建。
    if (!same_owner(window, record) || !visible_top_level(window))
        return;
    const auto latest = bounds(window);
    if (latest.location != current.location || !same_size(latest, current))
        return;
    move_window(window, target);
}

void restore_app_window(const fs::path& journal) {
    if (!fs::exists(journal))
        return;
    const auto record = load(journal);
    const auto window = reinterpret_cast<HWND>(record.id.window);
    if (same_owner(window, record)) {
        if (IsIconic(window)) {
            // 最小化窗口只恢复“还原后的位置”，不把它强行展开到前台。
            auto current = placement(window);
            const auto normal = frame(current.rcNormalPosition);
            if (same_size(normal, record.normal) &&
                (normal.location == record.previous_normal || normal.location == record.next_normal)) {
                const auto original = record.normal.location;
                current.rcNormalPosition = {original.x, original.y, original.x + normal.width, original.y + normal.height};
                if (!SetWindowPlacement(window, &current))
                    throw std::system_error(GetLastError(), std::system_category(), "Restore minimized application window");
            }
        } else if ((record.fullscreen || !IsZoomed(window)) && owns_position(record, bounds(window))) {
            move_window(window, record.original.location);
        }
    }
    fs::remove(journal);
}
}
