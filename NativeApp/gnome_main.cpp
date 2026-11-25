// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_frame.hpp"
#include "ui_text.hpp"
#include <libayatana-appindicator/app-indicator.h>
#include <wx/app.h>
#include <wx/frame.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

class SettingsApp final : public wxApp {
    bool OnInit() override {
        bool background = false;
        for (int i = 1; i < argc; ++i)
            background = background || argv[i] == "--background";
        SetAppName("OpenOLEDShifter");
        application_ = g_application_new("org.openoledshifter.Desktop", G_APPLICATION_DEFAULT_FLAGS);
        if (!g_application_register(application_, nullptr, nullptr)) {
            g_clear_object(&application_);
        } else if (g_application_get_is_remote(application_)) {
            if (!background)
                g_application_activate(application_);
            g_clear_object(&application_);
            return false;
        }
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
        auto* frame = oled::create_settings_frame();
        SetTopWindow(frame);
        init_tray();
        frame->Bind(wxEVT_CLOSE_WINDOW, [this, frame](wxCloseEvent& event) {
            if (event.CanVeto() && has_tray()) {
                frame->Hide();
                event.Veto();
            } else {
                event.Skip();
            }
        });
        if (application_) {
            g_signal_connect(application_, "activate", G_CALLBACK(+[](GApplication*, gpointer data) {
                auto* app = static_cast<SettingsApp*>(data);
                app->CallAfter([app] {
                    if (auto* window = app->GetTopWindow()) {
                        window->Show();
                        window->Raise();
                    }
                });
            }), this);
            auto* quit = g_simple_action_new("quit", nullptr);
            g_signal_connect(quit, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                auto* app = static_cast<SettingsApp*>(data);
                app->CallAfter([app] {
                    if (auto* window = app->GetTopWindow())
                        window->Close(true);
                });
            }), this);
            g_action_map_add_action(G_ACTION_MAP(application_), G_ACTION(quit));
            g_object_unref(quit);
        }
        if (!background || !application_)
            frame->Show();
        return true;
    }

    int OnExit() override {
        if (tray_)
            app_indicator_set_status(tray_, APP_INDICATOR_STATUS_PASSIVE);
        g_clear_object(&tray_);
        g_clear_object(&application_);
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

    void init_tray() {
        tray_ = app_indicator_new("openoledshifter", "org.openoledshifter.Desktop",
                                  APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
        auto* menu = gtk_menu_new();
        const auto add = [&](const char* label, GCallback callback) {
            const auto label_text = oled::ui_text(label);
            const auto text = label_text.utf8_str();
            auto* item = gtk_menu_item_new_with_label(text.data());
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_signal_connect(item, "activate", callback, this);
            return item;
        };
        auto* open = add("Open settings", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            auto* window = static_cast<SettingsApp*>(data)->GetTopWindow();
            if (window) {
                window->Show();
                window->Raise();
            }
        }));
        add("Pause / continue", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            oled::send_tray_command(static_cast<SettingsApp*>(data)->GetTopWindow(), oled::Command::Toggle);
        }));
        add("Shift once", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            oled::send_tray_command(static_cast<SettingsApp*>(data)->GetTopWindow(), oled::Command::Shift);
        }));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        add("Exit and restore", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            oled::send_tray_command(static_cast<SettingsApp*>(data)->GetTopWindow(), oled::Command::Pause, true);
        }));
        gtk_widget_show_all(menu);
        app_indicator_set_menu(tray_, GTK_MENU(menu));
        app_indicator_set_secondary_activate_target(tray_, open);
        app_indicator_set_status(tray_, APP_INDICATOR_STATUS_ACTIVE);
    }

    bool has_tray() const {
        auto* connection = application_ ? g_application_get_dbus_connection(application_) : nullptr;
        if (!tray_ || !connection)
            return false;
        auto* reply = g_dbus_connection_call_sync(connection, "org.kde.StatusNotifierWatcher",
            "/StatusNotifierWatcher", "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.kde.StatusNotifierWatcher", "IsStatusNotifierHostRegistered"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 300, nullptr, nullptr);
        if (!reply)
            return false;
        GVariant* available = nullptr;
        g_variant_get(reply, "(v)", &available);
        const bool registered = g_variant_is_of_type(available, G_VARIANT_TYPE_BOOLEAN) &&
                                g_variant_get_boolean(available);
        g_variant_unref(available);
        g_variant_unref(reply);
        return registered;
    }

    AppIndicator* tray_ = nullptr;
    GApplication* application_ = nullptr;
    GSettings* appearance_ = nullptr;
};

wxIMPLEMENT_APP(SettingsApp);
