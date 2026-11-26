// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "app/settings_frame.hpp"
#include "app/settings_backend.hpp"
#include "windows_host.hpp"
#include "app/ui_text.hpp"

#include <wx/app.h>
#include <wx/bmpbndl.h>
#include <wx/icon.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/taskbar.h>
#include <wx/timer.h>
#include <windows.h>
#include <wtsapi32.h>

#include <stdexcept>
#include <string>

namespace {
constexpr auto window_class = L"OpenOLEDShifterNativeSession";
constexpr auto wm_activate_settings = WM_APP + 1;
constexpr int tray_toggle = wxID_HIGHEST + 1;
constexpr int tray_shift = wxID_HIGHEST + 2;

LRESULT CALLBACK session_proc(HWND window, UINT message, WPARAM param, LPARAM value) {
    using namespace oled::win;
    switch (message) {
    case WM_WTSSESSION_CHANGE:
        if (param == WTS_SESSION_LOCK || param == WTS_SESSION_UNLOCK)
            set_locked(param == WTS_SESSION_LOCK);
        break;
    case WM_POWERBROADCAST:
        if (param == PBT_APMSUSPEND)
            set_suspended(true);
        else if (param == PBT_APMRESUMEAUTOMATIC || param == PBT_APMRESUMESUSPEND)
            set_suspended(false);
        return TRUE;
    case WM_DISPLAYCHANGE:
        displays_changed();
        break;
    case WM_QUERYENDSESSION:
        set_ending(true);
        return TRUE;
    case WM_ENDSESSION:
        if (param)
            shutdown();
        else
            set_ending(false);
        return 0;
    case wm_activate_settings:
        if (auto* frame = wxDynamicCast(wxTheApp->GetTopWindow(), wxFrame)) {
            frame->Show();
            frame->Iconize(false);
            frame->Raise();
        }
        return 0;
    }
    return DefWindowProcW(window, message, param, value);
}

class Tray final : public wxTaskBarIcon {
public:
    explicit Tray(HWND session) {
        Bind(wxEVT_TASKBAR_LEFT_UP, [session](wxTaskBarIconEvent&) { PostMessageW(session, wm_activate_settings, 0, 0); });
        Bind(wxEVT_MENU, [session](wxCommandEvent&) { PostMessageW(session, wm_activate_settings, 0, 0); }, wxID_OPEN);
        Bind(wxEVT_MENU, [](wxCommandEvent&) {
            wxTheApp->CallAfter([] {
                oled::send_tray_command(wxTheApp->GetTopWindow(), oled::Command::Toggle);
            });
        }, tray_toggle);
        Bind(wxEVT_MENU, [](wxCommandEvent&) {
            wxTheApp->CallAfter([] { oled::send_tray_command(wxTheApp->GetTopWindow(), oled::Command::Shift); });
        }, tray_shift);
        Bind(wxEVT_MENU, [](wxCommandEvent&) { wxTheApp->ExitMainLoop(); }, wxID_EXIT);
    }

private:
    wxMenu* CreatePopupMenu() override {
        auto* menu = new wxMenu;
        menu->Append(wxID_OPEN, oled::ui_text("Open settings"));
        menu->Append(tray_toggle, oled::ui_text("Pause / continue"));
        menu->Append(tray_shift, oled::ui_text("Shift once"));
        menu->AppendSeparator();
        menu->Append(wxID_EXIT, oled::ui_text("Exit and restore"));
        return menu;
    }
};

class WindowsApp final : public wxApp {
    bool OnInit() override {
        try {
            for (int i = 1; i < argc; ++i) {
                smoke_ = smoke_ || argv[i] == "--smoke-test";
                background_ = background_ || argv[i] == "--background";
            }
            if (!smoke_ && !acquire_instance())
                return false;
            if (!SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                throw std::runtime_error("Cannot use physical-pixel monitor coordinates");
            oled::win::initialize(smoke_);
            WNDCLASSW registration{};
            registration.lpfnWndProc = session_proc;
            registration.hInstance = GetModuleHandleW(nullptr);
            registration.lpszClassName = window_class;
            if (!RegisterClassW(&registration))
                throw std::runtime_error("Cannot register session notifications");
            session_ = CreateWindowExW(0, window_class, L"", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, registration.hInstance, nullptr);
            if (!session_)
                throw std::runtime_error("Cannot create session notification window");
            if (!smoke_ && !WTSRegisterSessionNotification(session_, NOTIFY_FOR_THIS_SESSION))
                throw std::runtime_error("Cannot subscribe to Windows session changes");
            registered_ = !smoke_;
            SetAppName("OpenOLEDShifter");
            MSWEnableDarkMode(DarkMode_Auto);
            auto* frame = oled::create_settings_frame();
            SetTopWindow(frame);
            frame->Bind(wxEVT_CLOSE_WINDOW, [frame](wxCloseEvent& event) {
                if (event.CanVeto()) {
                    frame->Hide();
                    event.Veto();
                } else {
                    event.Skip();
                }
            });
            const wxIcon icon("OPENOLED_ICON", wxBITMAP_TYPE_ICO_RESOURCE);
            if (!icon.IsOk())
                throw std::runtime_error("Cannot load the embedded Windows icon");
            frame->SetIcon(icon);
            tray_ = new Tray(session_);
            if (!tray_->SetIcon(wxBitmapBundle::FromBitmap(wxBitmap(icon)), "OpenOLEDShifter"))
                throw std::runtime_error("Cannot register the Windows tray icon");
            SetExitOnFrameDelete(false);
            if (!background_ || smoke_)
                frame->Show();
            timer_.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
                if (smoke_) {
                    ExitMainLoop();
                    return;
                }
                if (activation_ && WaitForSingleObject(activation_, 0) == WAIT_OBJECT_0)
                    PostMessageW(session_, wm_activate_settings, 0, 0);
                oled::win::tick();
            });
            timer_.Start(smoke_ ? 700 : 1000);
            return true;
        } catch (const std::exception& error) {
            clean_up();
            if (!smoke_)
                wxMessageBox(wxString::FromUTF8(error.what()), "OpenOLEDShifter", wxOK | wxICON_ERROR);
            return false;
        }
    }

    int OnRun() override {
        int result = 1;
        try {
            result = wxApp::OnRun();
        } catch (...) {
            oled::win::shutdown();
            clean_up();
            throw;
        }
        // wxWidgets destroys top-level windows before OnExit(), including
        // the taskbar icon's hidden window. Dispose the icon while it is alive.
        const bool restored = oled::win::shutdown();
        clean_up();
        if (!restored)
            wxMessageBox(oled::ui_text("Desktop recovery is pending. Reopen OpenOLEDShifter to retry."),
                         "OpenOLEDShifter", wxOK | wxICON_WARNING);
        return restored ? result : 1;
    }

private:
    bool acquire_instance() {
        wchar_t name[257]{};
        DWORD size = 257;
        if (!GetUserNameW(name, &size))
            throw std::runtime_error("Cannot identify the Windows user");
        const auto mutex_name = std::wstring(L"Local\\OpenOLEDShifter-") + name;
        mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
        const auto error = GetLastError();
        if (!mutex_)
            throw std::runtime_error("Cannot check whether OpenOLEDShifter is already running");
        const auto activation_name = std::wstring(L"Local\\OpenOLEDShifter-Settings-") + name;
        activation_ = CreateEventW(nullptr, FALSE, FALSE, activation_name.c_str());
        if (!activation_)
            throw std::runtime_error("Cannot open the settings activation event");
        if (error == ERROR_ALREADY_EXISTS) {
            SetEvent(activation_);
            clean_up();
            return false;
        }
        return true;
    }

    void clean_up() {
        timer_.Stop();
        delete tray_;
        tray_ = nullptr;
        if (registered_)
            WTSUnRegisterSessionNotification(session_);
        if (session_)
            DestroyWindow(session_);
        if (activation_)
            CloseHandle(activation_);
        if (mutex_)
            CloseHandle(mutex_);
        registered_ = false;
        session_ = nullptr;
        activation_ = mutex_ = nullptr;
    }

    wxTimer timer_;
    Tray* tray_ = nullptr;
    HWND session_ = nullptr;
    HANDLE mutex_ = nullptr, activation_ = nullptr;
    bool registered_ = false, smoke_ = false, background_ = false;
};
}

wxIMPLEMENT_APP(WindowsApp);
