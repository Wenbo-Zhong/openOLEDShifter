// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <wx/string.h>
#include <string_view>

namespace oled {
wxString ui_text(std::string_view key);
wxString ui_status(std::string_view status);
}
