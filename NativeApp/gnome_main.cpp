// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_frame.hpp"
#include <wx/app.h>

class SettingsApp final : public wxApp {
    bool OnInit() override {
        SetAppName("OpenOLEDShifter");
        oled::create_settings_frame()->Show();
        return true;
    }
};

wxIMPLEMENT_APP(SettingsApp);
