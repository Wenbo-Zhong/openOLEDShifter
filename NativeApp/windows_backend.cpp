// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_backend.hpp"
#include "windows_app_windows.hpp"
#include "windows_desktop.hpp"
#include "windows_displays.hpp"
#include "windows_host.hpp"
#include "windows_storage.hpp"
#include "windows_wallpaper.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <shlobj.h>
#include <sstream>
#include <tlhelp32.h>

namespace oled {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
constexpr auto run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto run_value = L"OpenOLEDShifterNative";

void check_win(LSTATUS error, const char* action) {
    if (error != ERROR_SUCCESS)
        throw std::system_error(error, std::system_category(), action);
}

fs::path data_root() {
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(path, CoTaskMemFree);
    if (FAILED(result) || !path)
        throw std::runtime_error("Cannot locate LocalAppData");
    return path;
}

bool is_registered() {
    std::wstring command(32768, L'\0');
    DWORD bytes = static_cast<DWORD>(command.size() * sizeof(wchar_t));
    const auto result = RegGetValueW(HKEY_CURRENT_USER, run_key, run_value, RRF_RT_REG_SZ,
                                    nullptr, command.data(), &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        return false;
    check_win(result, "Read login startup");
    if (bytes < sizeof(wchar_t))
        return false;
    command.resize(bytes / sizeof(wchar_t) - 1);
    const auto end = command.find(L'"', 1);
    return command.starts_with(L'"') && end != std::wstring::npos &&
           command.substr(end + 1) == L" --background" && fs::is_regular_file(command.substr(1, end - 1));
}

void register_startup(bool enabled) {
    if (!enabled) {
        const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, run_value);
        if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
            check_win(result, "Disable login startup");
        return;
    }
    std::wstring executable(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (size == 0 || size == executable.size())
        throw std::runtime_error("Cannot read executable location");
    executable.resize(size);
    const auto installed = data_root() / L"Programs" / L"OpenOLEDShifter" / L"OpenOLEDShifter.exe";
    fs::create_directories(installed.parent_path());
    if (!fs::exists(installed) || !fs::equivalent(executable, installed)) {
        const auto staged = installed.wstring() + L".new";
        fs::copy_file(executable, staged, fs::copy_options::overwrite_existing);
        if (!MoveFileExW(staged.c_str(), installed.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto error = GetLastError();
            std::error_code ignored;
            fs::remove(staged, ignored);
            check_win(error, "Install login executable");
        }
    }
    const auto command = L"\"" + installed.wstring() + L"\" --background";
    check_win(RegSetKeyValueW(HKEY_CURRENT_USER, run_key, run_value, REG_SZ, command.c_str(),
                             static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))), "Enable login startup");
    if (!is_registered())
        throw std::runtime_error("Windows did not retain the login startup entry");
}

bool vendor_running() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot check whether ASUS OLED Shifter is running");
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    bool found = false;
    if (Process32FirstW(snapshot, &process)) {
        do {
            if (_wcsicmp(process.szExeFile, L"AsusOLEDShifter.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &process));
    } else {
        const auto error = GetLastError();
        CloseHandle(snapshot);
        check_win(error, "Read running processes");
        return false;
    }
    CloseHandle(snapshot);
    return found;
}

bool interactive_desktop() {
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop)
        return false;
    wchar_t name[128]{};
    const auto read = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), nullptr);
    CloseDesktop(desktop);
    return read && _wcsicmp(name, L"Default") == 0;
}

bool legacy_needs_recovery(const fs::path& journal) {
    if (!fs::exists(journal))
        return false;
    std::ifstream stream(journal, std::ios::binary);
    std::string value{std::istreambuf_iterator<char>(stream), {}};
    std::erase_if(value, [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
    return value != "{\"Windows\":[],\"Wallpapers\":[],\"TaskbarOriginal\":null,\"TaskbarLast\":null}";
}

std::string screen_label(const win::WallpaperDisplay& display) {
    // Win32 display names are ASCII, for example \\.\DISPLAY1.
    std::string name(display.screen_name.begin(), display.screen_name.end());
    name += " (" + std::to_string(display.bounds.right - display.bounds.left) + " x " +
            std::to_string(display.bounds.bottom - display.bounds.top) + ")";
    if (display.primary)
        name += " - primary";
    return name;
}

std::wstring join_processes(const std::vector<std::wstring>& processes) {
    std::wstring names;
    for (const auto& process : processes) {
        if (!names.empty())
            names += L", ";
        names += process;
    }
    return names;
}

class WindowsBackend final : public Backend {
public:
    const char* description() const override {
        return "The selected display applies to wallpaper and application windows. "
               "Desktop icons and the taskbar apply system-wide.";
    }
    const char* failure_hint() const override { return "Windows settings operation failed."; }
    DesktopFeatures features() const override { return {true, true, true, true, true, true, true}; }
    bool use_worker() const override { return false; }

    void initialize(bool smoke) {
        smoke_ = smoke;
        if (smoke_) {
            status_ = "Tray smoke test: desktop changes are disabled.";
            return;
        }
        const auto root = data_root();
        config_ = root / L"OpenOLEDShifterNative" / L"settings.ini";
        icons_journal_ = config_.parent_path() / L"desktop-icons.recovery";
        taskbar_journal_ = config_.parent_path() / L"taskbar.recovery";
        fullscreen_journal_ = config_.parent_path() / L"fullscreen-shell.recovery";
        legacy_journal_ = root / L"OpenOLEDShifter" / L"recovery.json";
        fs::create_directories(config_.parent_path());
        const auto read = [this](const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(L"Shifter", key, fallback, config_.c_str()));
        };
        settings_.interval = std::clamp(read(L"IntervalSeconds", default_interval), min_interval, max_interval);
        settings_.radius = std::clamp(read(L"Radius", default_radius), min_radius, max_radius);
        settings_.enabled = read(L"Enabled", 0) == 1;
        settings_.wallpaper = read(L"Wallpaper", 1) == 1;
        settings_.desktop_icons = read(L"DesktopIcons", 0) == 1;
        settings_.taskbar_auto_hide = read(L"TaskbarAutoHide", 0) == 1;
        settings_.pause_fullscreen = read(L"PauseFullscreen", 1) == 1;
        settings_.shift_fullscreen_shell = read(L"ShiftFullscreenShell", 0) == 1;
        wchar_t names[2049]{};
        GetPrivateProfileStringW(L"Shifter", L"WindowProcesses", L"", names, 2049, config_.c_str());
        window_processes_ = win::parse_window_processes(names);
        settings_.window_processes = win::to_utf8(join_processes(window_processes_));
        // Read the ASCII key through the wide profile API so non-ASCII profile paths work.
        wchar_t wide_selection[64]{};
        GetPrivateProfileStringW(L"Shifter", L"WallpaperDisplay", L"primary", wide_selection, 64, config_.c_str());
        std::string selected;
        for (const wchar_t* ch = wide_selection; *ch; ++ch) {
            if (*ch > 127) {
                selected.clear();
                break;
            }
            selected.push_back(static_cast<char>(*ch));
        }
        if (win::valid_display_selection(selected))
            settings_.wallpaper_display = std::move(selected);
        schedule_.reset(settings_.interval, Schedule::Clock::now());
        if (has_pending_recovery()) {
            try {
                if (vendor_running())
                    throw std::runtime_error("Close ASUS OLED Shifter before recovering a previous shift");
                restore();
            } catch (const std::exception& error) {
                failed_ = true;
                status_ = std::string("Recovery pending: ") + error.what();
                return;
            }
        }
        tick();
    }

    State call(Command command, const Settings& settings) override {
        if (smoke_ || command == Command::Read)
            return state();
        if (command == Command::SetStartup) {
            try {
                register_startup(settings.login);
                startup_status_ = settings.login ? "Windows login startup is registered."
                                                 : "Windows login startup was removed.";
            } catch (const std::exception& error) {
                startup_status_ = std::string("Could not change Windows login startup: ") + error.what();
            }
            return state();
        }
        if (command == Command::Toggle) {
            auto requested = settings_;
            requested.enabled = !requested.enabled;
            return call(requested.enabled ? Command::Apply : Command::Pause, requested);
        }
        if (command == Command::Apply || command == Command::Pause) {
            try {
                auto requested = command == Command::Pause ? paused_settings() : settings;
                if (requested.interval < min_interval || requested.interval > max_interval ||
                    requested.radius < min_radius || requested.radius > max_radius ||
                    !win::valid_display_selection(requested.wallpaper_display))
                    throw std::invalid_argument("Settings are out of range");
                auto processes = win::parse_window_processes(win::from_utf8(requested.window_processes));
                const auto names = join_processes(processes);
                if (names.size() > 2048)
                    throw std::invalid_argument("Application process list is too long");
                requested.window_processes = win::to_utf8(names);
                if (has_pending_recovery() && vendor_running())
                    throw std::runtime_error("Close ASUS OLED Shifter before recovering a previous shift");
                restore();
                save(requested);
                settings_ = requested;
                window_processes_ = std::move(processes);
                failed_ = false;
                schedule_.reset(settings_.interval, Schedule::Clock::now());
                tick();
            } catch (const std::exception& error) {
                failed_ = true;
                status_ = std::string("Paused: ") + error.what();
            }
        } else if (command == Command::Shift) {
            tick(true);
        }
        return state();
    }

    void tick(bool force = false) {
        if (smoke_ || failed_)
            return;
        try {
            update_displays(force);
            const auto foreground = GetForegroundWindow();
            const bool full = win::fullscreen_window(foreground);
            const auto shell = full && settings_.shift_fullscreen_shell &&
                               selected_window_display(foreground) && win::known_fullscreen_shell(foreground)
                             ? foreground : nullptr;
            if (fs::exists(fullscreen_journal_) && (!shell || win::journaled_app_window(fullscreen_journal_) != shell))
                win::restore_app_window(fullscreen_journal_);
            const auto reason = pause_reason(full && !shell);
            const auto result = schedule_.poll(Schedule::Clock::now(), reason, force);
            if (!reason.empty()) {
                if (result.restore)
                    restore();
                status_ = reason;
                return;
            }
            if (result.shift) {
                shift(shell);
            } else if (!shifted_) {
                status_ = "Waiting for the next wallpaper shift";
            }
        } catch (const std::exception& error) {
            failed_ = true;
            status_ = std::string("Paused: ") + error.what();
            try { restore(); }
            catch (const std::exception& recovery) {
                status_ += std::string("; recovery pending: ") + recovery.what();
            }
        }
    }

    void set_locked(bool value) { locked_ = value; tick(); }
    void set_suspended(bool value) { suspended_ = value; tick(); }
    void set_ending(bool value) { ending_ = value; tick(); }

    void displays_changed() {
        if (smoke_ || !shutdown())
            return;
        next_display_scan_ = {};
        schedule_.reset(settings_.interval, Schedule::Clock::now());
        tick();
    }

    bool shutdown() {
        if (smoke_)
            return true;
        try {
            restore();
            return true;
        } catch (const std::exception& error) {
            failed_ = true;
            status_ = std::string("Recovery pending: ") + error.what();
            return false;
        }
    }

private:
    State state() const {
        State result{settings_, status_, {{"primary", "Primary display"}, {"all", "All displays"}}};
        if (!smoke_) {
            try { result.settings.login = is_registered(); }
            catch (const std::exception& error) {
                result.status += "\nCould not read Windows login startup: " + std::string(error.what());
            }
            if (!startup_status_.empty())
                result.status += "\n" + startup_status_;
        }
        for (const auto& display : displays_)
            result.wallpaper_displays.push_back({display.key(), screen_label(display)});
        if (deferred_wallpapers_)
            result.status += "\nReconnect displays to finish pending wallpaper recovery.";
        return result;
    }

    void update_displays(bool force) {
        const auto now = Schedule::Clock::now();
        if (!force && now < next_display_scan_)
            return;
        next_display_scan_ = now + 3s;
        std::vector<win::WallpaperDisplay> current;
        try {
            win::WallpaperSession session;
            current = win::active_wallpaper_displays(session.api());
        } catch (const std::exception&) {
            return; // A temporary enumeration error does not stop icon and taskbar support.
        }
        if (current != displays_) {
            displays_ = std::move(current);
            restore();
            schedule_.reset(settings_.interval, now);
        }
    }

    std::string pause_reason(bool fullscreen) const {
        if (ending_ || locked_ || suspended_ || !interactive_desktop())
            return "Paused: Windows session inactive";
        if (!settings_.enabled)
            return "Paused: timed shifts are turned off in the saved settings";
        if (!settings_.wallpaper && !settings_.desktop_icons && !settings_.taskbar_auto_hide &&
            window_processes_.empty() && !settings_.shift_fullscreen_shell)
            return "Paused: select at least one target";
        if (vendor_running())
            return "Paused: ASUS OLED Shifter is running";
        if (legacy_needs_recovery(legacy_journal_))
            return "Paused: open the previous version and choose Exit and restore first";
        if (settings_.pause_fullscreen && fullscreen)
            return "Paused: full-screen application";
        return {};
    }

    Settings paused_settings() const {
        auto copy = settings_;
        copy.enabled = false;
        return copy;
    }

    bool has_pending_recovery() const {
        return fs::exists(icons_journal_) || fs::exists(taskbar_journal_) ||
               fs::exists(fullscreen_journal_) || !win::app_window_journals(config_.parent_path()).empty() ||
               !win::wallpaper_journals(config_.parent_path()).empty();
    }

    template<class Action>
    static void attempt(std::string& errors, Action action) {
        try { action(); }
        catch (const std::exception& error) {
            if (!errors.empty())
                errors += "\n";
            errors += error.what();
        }
    }

    bool selected_window_display(HWND window) const {
        if (settings_.wallpaper_display == "all")
            return true;
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &info))
            return false;
        if (settings_.wallpaper_display == "primary")
            return (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        return std::ranges::any_of(displays_, [&](const auto& display) {
            return display.key() == settings_.wallpaper_display && EqualRect(&display.bounds, &info.rcMonitor);
        });
    }

    void shift(HWND fullscreen_shell) {
        const auto frames = make_cycle(settings_.radius);
        const auto next = frames.at(phase_).next_phase;
        const auto offset = frames.at(next).offset;
        std::string errors;
        if (settings_.wallpaper) attempt(errors, [&] {
            win::WallpaperSession session;
            auto& api = session.api();
            bool matched = false;
            for (const auto& display : displays_) {
                if (!win::selects_display(settings_.wallpaper_display, display))
                    continue;
                matched = true;
                attempt(errors, [&] {
                    win::shift_wallpaper(api, display.device_path,
                                         win::wallpaper_journal(config_.parent_path(), display), offset);
                });
            }
            if (!matched)
                throw std::runtime_error("Selected wallpaper display is unavailable");
        });
        if (settings_.desktop_icons) attempt(errors, [&] {
            win::shift_icon_layer(win::desktop_icon_layer(), icons_journal_, offset);
        });
        if (settings_.taskbar_auto_hide && !taskbar_engaged_) attempt(errors, [&] {
            win::hide_taskbar(win::explorer_taskbar(), taskbar_journal_);
            taskbar_engaged_ = true;
        });
        std::size_t matched_windows = 0;
        if (fullscreen_shell) attempt(errors, [&] {
            static const auto shell_cycle = make_cycle(default_radius);
            win::shift_app_window(fullscreen_shell, fullscreen_journal_,
                                  shell_cycle.at(next % shell_cycle.size()).offset, true);
            ++matched_windows;
        });
        attempt(errors, [&] {
            for (const auto window : win::allowed_app_windows(window_processes_, settings_.shift_fullscreen_shell)) {
                if (window == fullscreen_shell || win::fullscreen_window(window) || !selected_window_display(window))
                    continue;
                if (fs::exists(fullscreen_journal_) && win::journaled_app_window(fullscreen_journal_) == window)
                    continue;
                ++matched_windows;
                attempt(errors, [&] {
                    win::shift_app_window(window, win::app_window_journal(config_.parent_path(), window), offset, false);
                });
            }
        });
        phase_ = next;
        shifted_ = true;
        if (!errors.empty())
            status_ = "Some targets could not be updated:\n" + errors;
        else if (settings_.wallpaper || settings_.desktop_icons || matched_windows)
            status_ = "Offset (physical pixels): " + std::to_string(offset.x) + ", " + std::to_string(offset.y);
        else if (settings_.taskbar_auto_hide)
            status_ = "Taskbar auto-hide is active";
        else
            status_ = "No eligible application windows";
    }

    void restore() {
        std::string errors;
        attempt(errors, [&] {
            const auto journals = win::wallpaper_journals(config_.parent_path());
            deferred_wallpapers_ = 0;
            if (journals.empty())
                return;
            win::WallpaperSession session;
            auto& api = session.api();
            std::vector<std::wstring> active;
            for (const auto& display : win::active_wallpaper_displays(api))
                active.push_back(display.device_path);
            for (const auto& journal : journals)
                attempt(errors, [&] {
                    if (!win::restore_wallpaper(api, journal, active))
                        ++deferred_wallpapers_;
                });
        });
        attempt(errors, [&] {
            if (fs::exists(icons_journal_))
                win::restore_icon_layer(win::desktop_icon_layer(), icons_journal_);
        });
        attempt(errors, [&] { win::restore_app_window(fullscreen_journal_); });
        attempt(errors, [&] {
            for (const auto& journal : win::app_window_journals(config_.parent_path()))
                attempt(errors, [&] { win::restore_app_window(journal); });
        });
        attempt(errors, [&] {
            win::restore_taskbar(win::explorer_taskbar(), taskbar_journal_);
        });
        phase_ = 0;
        shifted_ = taskbar_engaged_ = false;
        if (!errors.empty())
            throw std::runtime_error(errors);
    }

    void save(const Settings& requested) const {
        std::wostringstream output;
        output << L"\ufeff[Shifter]\r\nIntervalSeconds=" << requested.interval << L"\r\nRadius="
               << requested.radius << L"\r\nEnabled=" << requested.enabled
               << L"\r\nWallpaper=" << requested.wallpaper << L"\r\nDesktopIcons=" << requested.desktop_icons
               << L"\r\nTaskbarAutoHide=" << requested.taskbar_auto_hide
               << L"\r\nWallpaperDisplay=" << win::from_utf8(requested.wallpaper_display)
               << L"\r\nPauseFullscreen=" << requested.pause_fullscreen
               << L"\r\nShiftFullscreenShell=" << requested.shift_fullscreen_shell
               << L"\r\nWindowProcesses=" << win::from_utf8(requested.window_processes) << L"\r\n";
        const auto contents = output.str();
        win::atomic_write(config_, std::as_bytes(std::span(contents)));
    }

    Settings settings_;
    Schedule schedule_{default_interval, Schedule::Clock::now()};
    fs::path config_, icons_journal_, taskbar_journal_, fullscreen_journal_, legacy_journal_;
    std::vector<win::WallpaperDisplay> displays_;
    std::vector<std::wstring> window_processes_;
    Schedule::Clock::time_point next_display_scan_{};
    std::size_t deferred_wallpapers_ = 0;
    std::string status_;
    std::string startup_status_;
    int phase_ = 0;
    bool smoke_ = false, locked_ = false, suspended_ = false, ending_ = false, failed_ = false;
    bool shifted_ = false, taskbar_engaged_ = false;
};

WindowsBackend& backend() {
    static WindowsBackend instance;
    return instance;
}
}

Backend& desktop_backend() { return backend(); }
void win::initialize(bool smoke) { backend().initialize(smoke); }
void win::tick() { backend().tick(); }
void win::set_locked(bool locked) { backend().set_locked(locked); }
void win::set_suspended(bool suspended) { backend().set_suspended(suspended); }
void win::set_ending(bool ending) { backend().set_ending(ending); }
void win::displays_changed() { backend().displays_changed(); }
bool win::shutdown() { return backend().shutdown(); }
}
