// SPDX-License-Identifier: GPL-3.0-only
#include "ui_text.hpp"
#include "core/localization.hpp"
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <glib.h>
#endif

namespace oled {
namespace {
const auto& catalog() {
    static const auto data = localization::load();
    return data;
}

const std::string& language() {
    static const auto selected = [] {
        wxString tag;
#ifdef _WIN32
        ULONG count = 0, size = 0;
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size) {
            std::wstring names(size, L'\0');
            if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, names.data(), &size))
                tag = names.c_str(); // First display language, independent of regional formats.
        }
#else
        tag = wxString::FromUTF8(g_get_language_names()[0]);
#endif
        return localization::select_locale(catalog(), tag.ToStdString(wxConvUTF8));
    }();
    return selected;
}

wxString utf8(std::string_view text) {
    return text.empty() ? wxString{} : wxString::FromUTF8(text.data(), text.size());
}

wxString status_line(std::string_view line) {
    std::string_view best;
    std::string_view localized;
    for (const auto& [key, text] : catalog().at("en")) {
        if (line == key)
            return ui_text(key);
        if (key.ends_with(' ') && line.starts_with(key) && key.size() > best.size()) {
            best = key;
            localized = localization::lookup(catalog(), language(), key);
        }
    }
    auto tail = line.substr(best.size());
    for (const std::string_view suffix : {" physical px", " - primary"})
        if (tail.ends_with(suffix))
            return utf8(localized) + utf8(tail.substr(0, tail.size() - suffix.size())) + ui_text(suffix);
    return utf8(localized) + utf8(tail);
}
}

wxString ui_text(std::string_view key) {
    return utf8(localization::lookup(catalog(), language(), key));
}

wxString ui_status(std::string_view status) {
    wxString output;
    while (!status.empty()) {
        const auto end = status.find('\n');
        output += status_line(status.substr(0, end));
        if (end == std::string_view::npos)
            break;
        output += '\n';
        status.remove_prefix(end + 1);
    }
    return output;
}
}
