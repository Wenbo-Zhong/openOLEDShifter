// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "platform/windows/windows_desktop.hpp"
#include "platform/windows/windows_app_windows.hpp"

#include <iostream>
#include <shellapi.h>

namespace {
namespace fs = std::filesystem;
using namespace oled;
using namespace oled::win;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Fixture {
    fs::path root = fs::current_path() / (L"OpenOLEDDesktopTest-" + std::to_wstring(GetCurrentProcessId()));
    HWND parent = nullptr;
    HWND child = nullptr;
    ~Fixture() {
        if (parent)
            DestroyWindow(parent);
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    void initialize() {
        require(fs::create_directory(root), "Cannot create private fixture directory");
        const int offscreen = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 60;
        parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"Static", L"", WS_POPUP,
                                 offscreen, 10, 120, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Cannot create fixture host");
        child = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_VISIBLE, 10, 20, 40, 35,
                                parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(child != nullptr, "Cannot create fixture child");
        ShowWindow(parent, SW_SHOWNOACTIVATE);
    }

    Offset position() const {
        RECT bounds{};
        require(GetWindowRect(child, &bounds), "Cannot read fixture bounds");
        POINT point{bounds.left, bounds.top};
        ScreenToClient(parent, &point);
        return {static_cast<int>(point.x), static_cast<int>(point.y)};
    }
};

struct FakeTaskbar final : Taskbar {
    explicit FakeTaskbar(HWND target, fs::path path) : target(target), journal(std::move(path)) {}
    HWND window() const override { return target; }
    UINT state(HWND) const override { return current; }
    void set_state(HWND, UINT value) const override {
        require(fs::exists(journal), "Taskbar changed before recovery was persisted");
        current = value;
        ++writes;
    }
    HWND target;
    fs::path journal;
    mutable UINT current = 0;
    mutable unsigned writes = 0;
};
}

int main() try {
    Fixture fixture;
    fixture.initialize();
    const auto icons = fixture.root / L"icons.recovery";
    shift_icon_layer(fixture.child, icons, {1, 1});
    require(fixture.position() == Offset{11, 21} && fs::exists(icons), "First icon shift failed");
    shift_icon_layer(fixture.child, icons, {-1, 0});
    require(fixture.position() == Offset{9, 20}, "Icon shifts accumulated");
    restore_icon_layer(fixture.child, icons);
    require(fixture.position() == Offset{10, 20} && !fs::exists(icons), "Icon restoration failed");
    shift_icon_layer(fixture.child, icons, {1, 0});
    require(SetWindowPos(fixture.child, nullptr, 30, 45, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), "Cannot change fixture position");
    restore_icon_layer(fixture.child, icons);
    require(fixture.position() == Offset{30, 45} && !fs::exists(icons), "Restored an externally moved icon layer");

    FakeTaskbar taskbar(fixture.parent, fixture.root / L"taskbar.recovery");
    taskbar.current = ABS_AUTOHIDE;
    hide_taskbar(taskbar, taskbar.journal);
    require(taskbar.writes == 0 && !fs::exists(taskbar.journal), "Claimed an existing auto-hide setting");
    taskbar.current = ABS_ALWAYSONTOP;
    hide_taskbar(taskbar, taskbar.journal);
    require(taskbar.current == (ABS_ALWAYSONTOP | ABS_AUTOHIDE), "Auto-hide lost existing taskbar flags");
    restore_taskbar(taskbar, taskbar.journal);
    require(taskbar.current == ABS_ALWAYSONTOP &&
            !fs::exists(taskbar.journal) && taskbar.writes == 2, "Owned taskbar setting was not restored");
    hide_taskbar(taskbar, taskbar.journal);
    taskbar.current = 0;
    restore_taskbar(taskbar, taskbar.journal);
    require(taskbar.current == 0 && taskbar.writes == 3 &&
            !fs::exists(taskbar.journal), "Overwrote externally changed taskbar setting");

    const auto window = fixture.parent;
    const auto journal = app_window_journal(fixture.root, window);
    const auto get_rect = [window] {
        RECT rect{};
        require(GetWindowRect(window, &rect), "Cannot read application fixture bounds");
        return rect;
    };
    const auto get_normal = [window] {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        require(GetWindowPlacement(window, &placement), "Cannot read application fixture placement");
        return placement.rcNormalPosition;
    };
    const auto original = get_rect();
    const auto normal = get_normal();
    shift_app_window(window, journal, {-1, 1}, false);
    require(get_rect().left == original.left - 1 && fs::exists(journal), "Application window did not move");
    require(app_window_journals(fixture.root).size() == 1 && journaled_app_window(journal) == window,
            "Cannot recover application fixture journal");
    shift_app_window(window, journal, {-2, 1}, false);
    require(get_rect().left == original.left - 2, "Application window shifts accumulated");
    ShowWindow(window, SW_SHOWMINNOACTIVE);
    require(IsIconic(window), "Application fixture failed to minimize");
    restore_app_window(journal);
    const auto after_minimize = get_normal();
    require(IsIconic(window) && EqualRect(&normal, &after_minimize) && !fs::exists(journal),
            "Restoration changed the minimized state or failed to restore placement");

    ShowWindow(window, SW_SHOWNOACTIVATE);
    require(!IsIconic(window), "Cannot show the application fixture after minimizing");
    const auto before_manually_moved = get_rect();
    shift_app_window(window, journal, {-1, 0}, true);
    require(SetWindowPos(window, nullptr, before_manually_moved.left - 17, before_manually_moved.top,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE),
            "Cannot change application fixture bounds");
    shift_app_window(window, journal, {-2, 0}, true);
    restore_app_window(journal);
    require(get_rect().left == before_manually_moved.left - 17 && !fs::exists(journal),
            "Overwrote manually moved application window");
    const auto names = parse_window_processes(L"notepad; C:\\Apps\\My App.EXE, NOTEPAD.EXE,\n 编辑器.exe");
    require(names == std::vector<std::wstring>{L"notepad.exe", L"My App.EXE", L"编辑器.exe"} &&
            from_utf8(to_utf8(names.back())) == names.back(), "Process allowlist parsing failed");
    require(!known_fullscreen_shell(window), "Ordinary application fixture was identified as a fullscreen shell");
    std::cout << "PASS: icon layer, taskbar and application window recovery\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
