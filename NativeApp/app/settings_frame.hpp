// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include <wx/frame.h>
#include "settings_backend.hpp"

namespace oled {
wxFrame* create_settings_frame();
void send_tray_command(wxWindow* frame, Command command, bool close_after = false);
}
