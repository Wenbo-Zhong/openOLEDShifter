// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_backend.hpp"
#include "settings_frame.hpp"

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <algorithm>
#include <future>
#include <utility>

namespace {
using namespace oled;

class SettingsFrame final : public wxFrame {
public:
    SettingsFrame() : wxFrame(nullptr, wxID_ANY, "OpenOLEDShifter"), timer_(this) {
        auto* panel = new wxPanel(this);
        auto* layout = new wxBoxSizer(wxVERTICAL);
        enabled_ = new wxCheckBox(panel, wxID_ANY, "Enable timed shifts");
        login_ = new wxCheckBox(panel, wxID_ANY, "Start shifting at login");
        interval_ = new wxSpinCtrl(panel, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, min_interval, max_interval, default_interval);
        radius_ = new wxSpinCtrl(panel, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, min_radius, max_radius, default_radius);
        auto* fields = new wxFlexGridSizer(2, 8, 12);
        fields->Add(new wxStaticText(panel, wxID_ANY, "Interval (seconds)"), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(interval_);
        fields->Add(new wxStaticText(panel, wxID_ANY, "Maximum shift (physical pixels)"), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(radius_);
        layout->Add(enabled_, 0, wxALL, 10);
        layout->Add(fields, 0, wxLEFT | wxRIGHT, 10);
        layout->Add(login_, 0, wxALL, 10);

        const auto features = desktop_backend().features();
        add_option(panel, layout, features.wallpaper_toggle, "Shift static wallpaper", &Settings::wallpaper);
        add_option(panel, layout, features.desktop_icons, "Shift desktop icons", &Settings::desktop_icons);
        add_option(panel, layout, features.taskbar_auto_hide, "Auto-hide taskbar while active", &Settings::taskbar_auto_hide);
        add_option(panel, layout, features.application_windows, "Pause for full-screen activity", &Settings::pause_fullscreen);
        add_option(panel, layout, features.application_windows, "Shift recognized full-screen shell windows", &Settings::shift_fullscreen_shell);
        if (features.wallpaper_monitor_selection) {
            auto* target = new wxBoxSizer(wxHORIZONTAL);
            target->Add(new wxStaticText(panel, wxID_ANY, "Wallpaper / window display"), 0,
                        wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
            display_ = new wxChoice(panel, wxID_ANY);
            display_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { dirty_ = true; });
            target->Add(display_, 1, wxALIGN_CENTER_VERTICAL);
            layout->Add(target, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        }
        if (features.application_windows) {
            layout->Add(new wxStaticText(panel, wxID_ANY, "Application processes to shift"), 0,
                        wxLEFT | wxRIGHT | wxBOTTOM, 10);
            processes_ = new wxTextCtrl(panel, wxID_ANY);
            processes_->SetHint("For example: notepad.exe, explorer.exe");
            processes_->SetMaxLength(2048);
            processes_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { dirty_ = true; });
            layout->Add(processes_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        }

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        for (const auto& [label, command] : {std::pair{"Save", Command::Apply},
                    {"Pause and restore", Command::Pause}, {"Shift once", Command::Shift}}) {
            auto* button = new wxButton(panel, wxID_ANY, label);
            button->Bind(wxEVT_BUTTON, [this, command](wxCommandEvent&) { begin(command); });
            buttons->Add(button, 0, wxRIGHT, 6);
            buttons_.push_back(button);
        }
        layout->Add(buttons, 0, wxALL, 10);
        status_ = new wxStaticText(panel, wxID_ANY, "Loading settings…");
        layout->Add(status_, 0, wxEXPAND | wxALL, 10);
        auto* note = new wxStaticText(panel, wxID_ANY,
            wxString::FromUTF8(desktop_backend().description()));
        note->Wrap(420);
        layout->Add(note, 0, wxALL, 10);
        panel->SetSizer(layout);
        auto* outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(panel, 1, wxEXPAND);
        SetSizerAndFit(outer);
        SetMinSize(GetSize());

        for (auto* checkbox : {enabled_, login_})
            checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { dirty_ = true; });
        for (auto* spin : {interval_, radius_}) {
            spin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { dirty_ = true; });
            spin->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { dirty_ = true; });
        }
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { poll(); });
        timer_.Start(100);
        begin(Command::Read);
    }

private:
    void add_option(wxPanel* panel, wxBoxSizer* layout, bool supported,
                    const char* label, bool Settings::* value) {
        if (!supported)
            return;
        auto* checkbox = new wxCheckBox(panel, wxID_ANY, label);
        checkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { dirty_ = true; });
        layout->Add(checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
        options_.emplace_back(checkbox, value);
    }

    void controls() {
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

    void begin(Command command) {
        if (pending_.valid())
            return;
        command_ = command;
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
            if (Schedule::Clock::now() >= next_read_)
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
                display_->Append(wxString::FromUTF8(choice.name));
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
            if (!dirty_ || command_ == Command::Apply || command_ == Command::Pause) {
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
            status_->SetLabel(wxString::FromUTF8(state.status));
        } catch (const std::exception& error) {
            connected_ = false;
            status_->SetLabel(wxString::FromUTF8(desktop_backend().failure_hint()) + "\n" +
                              wxString::FromUTF8(error.what()));
        }
        status_->Wrap(420);
        Layout();
        next_read_ = Schedule::Clock::now() + std::chrono::seconds(3);
        controls();
    }

    wxTimer timer_;
    wxCheckBox* enabled_;
    wxCheckBox* login_;
    wxSpinCtrl* interval_;
    wxSpinCtrl* radius_;
    wxStaticText* status_;
    wxChoice* display_ = nullptr;
    wxTextCtrl* processes_ = nullptr;
    std::vector<DisplayChoice> displays_;
    std::vector<wxButton*> buttons_;
    std::vector<std::pair<wxCheckBox*, bool Settings::*>> options_;
    std::future<State> pending_;
    Schedule::Clock::time_point next_read_;
    Command command_ = Command::Read;
    bool connected_ = false;
    bool dirty_ = false;
};

}

wxFrame* oled::create_settings_frame() { return new SettingsFrame(); }
