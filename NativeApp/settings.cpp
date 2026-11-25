// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_backend.hpp"
#include "settings_frame.hpp"
#include <deque>
#include "ui_text.hpp"

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/display.h>
#include <wx/scrolwin.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <future>
#include <utility>

namespace {
using namespace oled;

class SettingsFrame final : public wxFrame {
public:
    SettingsFrame() : wxFrame(nullptr, wxID_ANY, "OpenOLEDShifter"), timer_(this) {
        panel_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL);
        auto* panel = panel_;
        panel_->SetScrollRate(0, FromDIP(16));
        auto* layout = new wxBoxSizer(wxVERTICAL);
        auto* heading = new wxStaticText(panel, wxID_ANY, ui_text("OLED pixel shifting"));
        heading->SetFont(heading->GetFont().Larger().Larger().Bold());
        layout->Add(heading, 0, wxALL, FromDIP(14));
        status_ = new wxStaticText(panel, wxID_ANY, status_text_);
        layout->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

        if (const auto* label = desktop_backend().setup_label()) {
            setup_ = new wxButton(panel, wxID_ANY, ui_text(label));
            setup_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { begin(Command::Setup); });
            layout->Add(setup_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));
        }

        const auto features = desktop_backend().features();
        immediate_startup_ = features.immediate_startup;
        columns_ = new wxBoxSizer(wxHORIZONTAL);
        auto* timing = new wxStaticBoxSizer(wxVERTICAL, panel, ui_text("Schedule"));
        columns_->Add(timing, 1, wxEXPAND | wxALL, FromDIP(5));
        auto* timing_parent = timing->GetStaticBox();
        enabled_ = new wxCheckBox(timing_parent, wxID_ANY, ui_text("Enable timed shifts"));
        login_ = new wxCheckBox(timing_parent, wxID_ANY,
                               ui_text(immediate_startup_ ? "Start Windows app at login" : desktop_backend().login_label()));
        interval_ = new wxSpinCtrl(timing_parent, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, min_interval, max_interval, default_interval);
        radius_ = new wxSpinCtrl(timing_parent, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, min_radius, max_radius, default_radius);
        timing->Add(enabled_, 0, wxALL, FromDIP(8));
        auto* fields = new wxFlexGridSizer(2, FromDIP(8), FromDIP(12));
        fields->AddGrowableCol(0);
        fields->Add(new wxStaticText(timing_parent, wxID_ANY, ui_text("Interval (seconds)")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(interval_, 0, wxEXPAND);
        fields->Add(new wxStaticText(timing_parent, wxID_ANY, ui_text("Maximum shift (physical pixels)")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(radius_, 0, wxEXPAND);
        timing->Add(fields, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
        if (desktop_backend().supports_login_startup())
            timing->Add(login_, 0, wxALL, FromDIP(8));
        else
            login_->Hide();
        add_option(timing_parent, timing, features.fullscreen_pause, "Pause for full-screen activity", &Settings::pause_fullscreen);

        if (features.wallpaper_toggle || features.desktop_icons || features.taskbar_auto_hide ||
            features.wallpaper_monitor_selection) {
            has_targets_ = true;
            auto* targets = new wxStaticBoxSizer(wxVERTICAL, panel, ui_text("Desktop targets"));
            columns_->Add(targets, 1, wxEXPAND | wxALL, FromDIP(5));
            auto* parent = targets->GetStaticBox();
            targets->AddSpacer(FromDIP(6));
            add_option(parent, targets, features.wallpaper_toggle, "Shift static wallpaper", &Settings::wallpaper);
            add_option(parent, targets, features.desktop_icons, "Shift desktop icons", &Settings::desktop_icons);
            add_option(parent, targets, features.taskbar_auto_hide, "Auto-hide taskbar while active", &Settings::taskbar_auto_hide);
            if (features.wallpaper_monitor_selection) {
                targets->Add(new wxStaticText(parent, wxID_ANY, ui_text("Wallpaper / window display")), 0,
                             wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
                display_ = new wxChoice(parent, wxID_ANY);
                display_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { mark_dirty(); });
                targets->Add(display_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
            }
        }
        layout->Add(columns_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(9));

        if (features.application_windows) {
            auto* applications = new wxStaticBoxSizer(wxVERTICAL, panel, ui_text("Application windows"));
            auto* parent = applications->GetStaticBox();
            applications->Add(new wxStaticText(parent, wxID_ANY, ui_text("Application processes to shift")), 0,
                              wxALL, FromDIP(8));
            processes_ = new wxTextCtrl(parent, wxID_ANY);
            processes_->SetHint(ui_text("For example: notepad.exe, explorer.exe"));
            processes_->SetMaxLength(2048);
            processes_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { mark_dirty(); });
            applications->Add(processes_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
            add_option(parent, applications, true, "Shift recognized full-screen shell windows", &Settings::shift_fullscreen_shell);
            layout->Add(applications, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
        }

        auto* buttons = new wxWrapSizer(wxHORIZONTAL);
        for (const auto& [label, command] : {std::pair{"Save", Command::Apply},
                    {"Pause and restore", Command::Pause}, {"Shift once", Command::Shift}}) {
            if (command == Command::Shift && !desktop_backend().supports_manual_shift())
                continue;
            auto* button = new wxButton(panel, wxID_ANY, ui_text(label));
            if (command == Command::Apply)
                button->SetDefault();
            button->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent&) { begin(command); });
            buttons->Add(button, 0, wxRIGHT, FromDIP(6));
            buttons_.push_back(button);
        }
        layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, FromDIP(14));


        note_ = new wxStaticText(panel, wxID_ANY,
            ui_text(desktop_backend().description()));

        layout->Add(note_, 0, wxEXPAND | wxALL, FromDIP(10));
        panel->SetSizer(layout);
        auto* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(panel, 1, wxEXPAND);
        SetSizer(outer);
        const auto work = wxDisplay(0u).GetClientArea().GetSize();
#ifdef __WXGTK__
        const auto preferred = FromDIP(wxSize(640, 580));
#else
        const auto preferred = FromDIP(wxSize(has_targets_ ? 740 : 515, 320));
#endif
        SetSize(wxSize(std::min(preferred.x, work.x), std::min(preferred.y, work.y)));
        SetMinSize(wxSize(std::min(FromDIP(410), work.x), std::min(FromDIP(240), work.y)));
        panel_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            wrap_labels();
        });
#ifdef _WIN32
        Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
            event.Skip();
            CallAfter([this] {
                panel_->SetScrollRate(0, FromDIP(16));
                wrap_width_ = 0;
                wrap_labels();
            });
        });
#endif

        enabled_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { mark_dirty(); });
        login_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (immediate_startup_)
                begin(Command::SetStartup);
            else
                mark_dirty();
        });
        for (auto* spin : {interval_, radius_}) {
            spin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { mark_dirty(); });
            spin->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { mark_dirty(); });
        }
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll(); });
        Layout();
        wrap_labels();
        const int frame_height = GetSize().y - GetClientSize().y;
        const auto content_height = layout->GetMinSize().y + frame_height + FromDIP(4);
#ifdef __WXGTK__
        SetSize(GetSize().x, std::min(work.y, std::max(preferred.y, content_height)));
#else
        SetSize(GetSize().x, std::min(work.y, content_height));
#endif
        timer_.Start(100);
        begin(Command::Read);
    }

    void send_from_tray(Command command, bool close_after) {
        tray_commands_.emplace_back(command, close_after);
        dispatch_tray_command();
    }

private:
    void dispatch_tray_command() {
        if (pending_.valid() || tray_commands_.empty())
            return;
        const auto [command, close_after] = tray_commands_.front();
        tray_commands_.pop_front();
        begin(command, true, close_after);
    }

    void mark_dirty() {
        if (!dirty_) {
            dirty_ = true;
            wrap_labels();
        }
    }

    void add_option(wxWindow* panel, wxBoxSizer* layout, bool supported,
                    const char* label, bool Settings::* value) {
        if (!supported)
            return;
        auto* checkbox = new wxCheckBox(panel, wxID_ANY, ui_text(label));
        checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { mark_dirty(); });
        layout->Add(checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
        options_.emplace_back(checkbox, value);
    }

    void controls() {
        if (setup_) {
            if (setup_->Show(!connected_))
                panel_->Layout();
            setup_->Enable(!pending_.valid());
        }
        for (auto* button : buttons_)
            button->Enable(connected_ && !pending_.valid());
        const bool editable = connected_ && (!pending_.valid() || command_ == Command::Read);
        enabled_->Enable(editable);
        login_->Enable(editable);
        interval_->Enable(editable);
        radius_->Enable(editable);
        if (display_)
            display_->Enable(editable);
        if (processes_)
            processes_->Enable(editable);
        for (const auto& [control, value] : options_)
            control->Enable(editable);
    }

    void begin(Command command, bool from_tray = false, bool close_after = false) {
        if (pending_.valid())
            return;
        command_ = command;
        from_tray_ = from_tray;
        close_after_ = close_after;
        Settings settings{interval_->GetValue(), radius_->GetValue(),
                          enabled_->GetValue(), login_->GetValue()};
        for (const auto& [control, value] : options_)
            settings.*value = control->GetValue();
        settings.wallpaper_display = selected_display();
        if (processes_)
            settings.window_processes = processes_->GetValue().ToStdString(wxConvUTF8);
        if (desktop_backend().use_worker()) {
            pending_ = std::async(std::launch::async, [command, settings] {
                return desktop_backend().call(command, settings);
            });
            controls();
        } else {
            complete([command, settings] { return desktop_backend().call(command, settings); });
        }
    }

    void poll() {
        using namespace std::chrono_literals;
        if (!pending_.valid()) {
            if (!tray_commands_.empty())
                dispatch_tray_command();
            else if (Schedule::Clock::now() >= next_read_)
                begin(Command::Read);
            return;
        }
        if (pending_.wait_for(0ms) != std::future_status::ready)
            return;
        complete([this] { return pending_.get(); });
    }

    std::string selected_display() const {
        if (!display_ || display_->GetSelection() == wxNOT_FOUND)
            return "primary";
        return displays_.at(display_->GetSelection()).id;
    }

    void update_displays(std::vector<DisplayChoice> choices, const std::string& selected) {
        if (!display_)
            return;
        const auto match = [&](const auto& choice) { return choice.id == selected; };
        if (std::ranges::none_of(choices, match))
            choices.push_back({selected, "Disconnected display"});
        if (choices != displays_) {
            display_->Clear();
            for (const auto& choice : choices)
                display_->Append(ui_status(choice.name));
            displays_ = std::move(choices);
        }
        const auto index = std::ranges::find_if(displays_, match) - displays_.begin();
        if (display_->GetSelection() != index)
            display_->SetSelection(static_cast<int>(index));
    }

    template<class ReadState>
    void complete(ReadState read_state) {
        try {
            auto state = read_state();
            connected_ = true;
            if (immediate_startup_)
                login_->SetValue(state.settings.login);
            if (!dirty_ || (!from_tray_ && (command_ == Command::Apply || command_ == Command::Pause))) {
                enabled_->SetValue(state.settings.enabled);
                login_->SetValue(state.settings.login);
                interval_->SetValue(state.settings.interval);
                radius_->SetValue(state.settings.radius);
                for (const auto& [control, value] : options_)
                    control->SetValue(state.settings.*value);
                if (processes_)
                    processes_->ChangeValue(wxString::FromUTF8(state.settings.window_processes));
                dirty_ = false;
            }
            const auto display = dirty_ ? selected_display() : state.settings.wallpaper_display;
            update_displays(std::move(state.wallpaper_displays), display);
            status_text_ = ui_status(state.status);
        } catch (const std::exception& error) {
            connected_ = false;
            status_text_ = ui_text(desktop_backend().failure_hint());
            if (*error.what())
                status_text_ += "\n" + ui_status(error.what());
        }
        wrap_labels();
#ifdef __WXGTK__
        if (!initial_status_fitted_) {
            initial_status_fitted_ = true;
            const auto display = wxDisplay::GetFromWindow(this);
            const auto work = wxDisplay(display == wxNOT_FOUND ? 0u : static_cast<unsigned>(display))
                .GetClientArea();
            const int missing = panel_->GetSizer()->GetMinSize().y + FromDIP(8) - panel_->GetClientSize().y;
            const auto size = GetSize();
            if (missing > 0 && size.y < work.height)
                SetSize(size.x, std::min(work.height, size.y + missing));
        }
#endif
        next_read_ = Schedule::Clock::now() + std::chrono::seconds(3);
        controls();
        if (close_after_) {
            tray_commands_.clear();
            CallAfter([this] { Close(true); });
        }
    }

    void wrap_labels() {
        const int content_width = panel_->GetClientSize().x;
        const int width = std::max(FromDIP(120), content_width - FromDIP(30));
        if (has_targets_) {
            const bool wide = content_width >= FromDIP(680);
            const int orientation = wide ? wxHORIZONTAL : wxVERTICAL;
            if (columns_->GetOrientation() != orientation) {
                columns_->SetOrientation(orientation);
                for (auto* group : columns_->GetChildren())
                    group->SetProportion(wide ? 1 : 0);
            }
        }
        const auto visible_status = dirty_
            ? status_text_ + "\n" + ui_text("Unsaved changes. Select Save to apply them.")
            : status_text_;
        if (width == wrap_width_ && last_status_ == visible_status)
            return;
        wrap_width_ = width;
        last_status_ = visible_status;
        status_->SetLabel(visible_status);
        status_->Wrap(width);
        note_->SetLabel(ui_text(desktop_backend().description()));
        note_->Wrap(width);
        panel_->Layout();
        panel_->FitInside();
    }

    wxScrolledWindow* panel_ = nullptr;
    wxBoxSizer* columns_ = nullptr;
    bool has_targets_ = false;
    bool immediate_startup_ = false;
    wxStaticText* note_ = nullptr;
    wxString status_text_ = ui_text("Loading settings…");
    wxString last_status_;
    int wrap_width_ = 0;
    wxTimer timer_;
    wxCheckBox* enabled_;
    wxCheckBox* login_;
    wxSpinCtrl* interval_;
    wxSpinCtrl* radius_;
    wxStaticText* status_;
    wxButton* setup_ = nullptr;
    wxChoice* display_ = nullptr;
    wxTextCtrl* processes_ = nullptr;
    std::vector<DisplayChoice> displays_;
    std::vector<wxButton*> buttons_;
    std::vector<std::pair<wxCheckBox*, bool Settings::*>> options_;
    std::future<State> pending_;
    std::deque<std::pair<Command, bool>> tray_commands_;
    bool from_tray_ = false;
    bool close_after_ = false;
    Schedule::Clock::time_point next_read_;
    Command command_ = Command::Read;
#ifdef __WXGTK__
    bool initial_status_fitted_ = false;
#endif
    bool connected_ = false;
    bool dirty_ = false;
};

}

wxFrame* oled::create_settings_frame() { return new SettingsFrame(); }

void oled::send_tray_command(wxWindow* frame, Command command, bool close_after) {
    if (auto* settings = dynamic_cast<SettingsFrame*>(frame))
        settings->send_from_tray(command, close_after);
}
