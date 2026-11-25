// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_frame.hpp"
#include <wx/app.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

class SettingsApp final : public wxApp {
    bool OnInit() override {
        SetAppName("OpenOLEDShifter");
        auto* source = g_settings_schema_source_get_default();
        auto* schema = source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : nullptr;
        if (schema) {
            if (g_settings_schema_has_key(schema, "color-scheme")) {
                appearance_ = g_settings_new_full(schema, nullptr, nullptr);
                g_signal_connect(appearance_, "changed::color-scheme", G_CALLBACK(color_changed), nullptr);
                color_changed(appearance_, nullptr, nullptr);
            }
            g_settings_schema_unref(schema);
        }
        oled::create_settings_frame()->Show();
        return true;
    }

    int OnExit() override {
        g_clear_object(&appearance_);
        return wxApp::OnExit();
    }

    static void color_changed(GSettings* settings, gchar*, gpointer) {
        if (auto* gtk = gtk_settings_get_default()) {
            auto* scheme = g_settings_get_string(settings, "color-scheme");
            g_object_set(gtk, "gtk-application-prefer-dark-theme",
                         g_strcmp0(scheme, "prefer-dark") == 0, nullptr);
            g_free(scheme);
        }
    }

    GSettings* appearance_ = nullptr;
};

wxIMPLEMENT_APP(SettingsApp);
