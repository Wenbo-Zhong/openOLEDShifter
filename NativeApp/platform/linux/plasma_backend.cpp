// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "linux_backend.hpp"

#include <gio/gio.h>

namespace oled {
namespace {
constexpr char plugin_id[] = "org.openoledshifter.wallpaper";

bool plasma_wallpaper_installed() {
    return linux_desktop::plugin_installed("plasma/wallpapers", plugin_id);
}

class PlasmaBackend final : public Backend {
    const char* description() const override { return "Save applies to all displays already using OpenOLEDShifter. Choose images per screen in Plasma wallpaper settings."; }
    const char* failure_hint() const override { return "Set up the Plasma wallpaper plugin"; }
    DesktopFeatures features() const override { return {.fullscreen_pause = true}; }
    const char* login_label() const override { return "Start KDE tray at login"; }
    const char* setup_label() const override { return "Open Plasma wallpaper settings"; }

    State call(Command command, const Settings& requested) override {
        if (command == Command::Setup) {
            if (plasma_wallpaper_installed()) {
                g_autoptr(GSubprocess) settings = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, nullptr,
                    "systemsettings", "kcm_wallpaper", nullptr);
                if (!settings)
                    throw std::runtime_error("Open wallpaper settings from the Plasma desktop menu.");
            }
            return call(Command::Read, {});
        }
        if (command != Command::Read && command != Command::Apply &&
            command != Command::Pause && command != Command::Toggle && command != Command::Shift)
            return {};
        if (!plasma_wallpaper_installed())
            throw std::runtime_error(
                std::string("Use the openoledshifter-plasma.zip package.\n") +
                "Extract the archive into the following folder. Create it if needed.\n"
                + g_get_user_data_dir() + "/plasma/wallpapers/\n"
                "Then reopen this window to check the installation.");

        g_autoptr(GDBusConnection) connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        if (!connection)
            throw std::runtime_error("Could not read the current Plasma wallpaper settings.");
        unsigned selected = 0, enabled = 0;
        Settings first;
        first.login = linux_desktop::autostart_enabled(linux_desktop::Desktop::Plasma);
        // Plasma screen IDs can have gaps after disconnecting a display.
        for (unsigned screen = 0; screen < 16; ++screen) {
            g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, "org.kde.plasmashell", "/PlasmaShell",
                "org.kde.PlasmaShell", "wallpaper", g_variant_new("(u)", screen),
                G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 500, nullptr, nullptr);
            if (!reply)
                throw std::runtime_error("Could not read the current Plasma wallpaper settings.");
            g_autoptr(GVariant) fields = g_variant_get_child_value(reply, 0);
            const char* plugin = nullptr;
            if (!g_variant_lookup(fields, "wallpaperPlugin", "&s", &plugin) ||
                std::string_view(plugin) != plugin_id)
                continue;

            gboolean checked = FALSE;
            const char* image = nullptr;
            gint32 interval = 0, radius = 0, serial = 0;
            gboolean pause_fullscreen = TRUE;
            g_variant_lookup(fields, "PauseFullscreen", "b", &pause_fullscreen);
            g_variant_lookup(fields, "ShiftRequest", "i", &serial);
            g_variant_lookup(fields, "Enabled", "b", &checked);
            g_variant_lookup(fields, "Interval", "i", &interval);
            g_variant_lookup(fields, "Radius", "i", &radius);
            const bool has_image = g_variant_lookup(fields, "Image", "&s", &image) &&
                                   g_str_has_prefix(image, "file:///");
            if (command != Command::Read && (command != Command::Shift || (checked && has_image))) {
                const bool next = command == Command::Apply ? requested.enabled :
                    command == Command::Toggle ? (selected == 0 ? !checked : first.enabled) :
                    command == Command::Shift ? static_cast<bool>(checked) : false;
                if (command == Command::Apply) {
                    interval = std::clamp(requested.interval, min_interval, max_interval);
                    radius = std::clamp(requested.radius, min_radius, max_radius);
                }
                GVariantBuilder updates;
                g_variant_builder_init(&updates, G_VARIANT_TYPE_VARDICT);
                if (command == Command::Shift)
                    g_variant_builder_add(&updates, "{sv}", "ShiftRequest",
                        g_variant_new_int32(serial == G_MAXINT32 ? 1 : serial + 1));
                else
                    g_variant_builder_add(&updates, "{sv}", "Enabled", g_variant_new_boolean(next));
                if (command == Command::Apply) {
                    g_variant_builder_add(&updates, "{sv}", "Interval", g_variant_new_int32(interval));
                    g_variant_builder_add(&updates, "{sv}", "Radius", g_variant_new_int32(radius));
                    g_variant_builder_add(&updates, "{sv}", "PauseFullscreen", g_variant_new_boolean(requested.pause_fullscreen));
                }
                g_autoptr(GVariant) changed = g_dbus_connection_call_sync(connection, "org.kde.plasmashell", "/PlasmaShell",
                    "org.kde.PlasmaShell", "setWallpaper",
                    g_variant_new("(s@a{sv}u)", plugin_id, g_variant_builder_end(&updates), screen),
                    G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, nullptr);
                if (!changed)
                    throw std::runtime_error("Could not update the Plasma wallpaper settings.");
                checked = next;
            }
            if (++selected == 1) {
                first.enabled = checked;
                first.pause_fullscreen = pause_fullscreen;
                first.interval = interval ? std::clamp(interval, min_interval, max_interval) : default_interval;
                first.radius = radius ? std::clamp(radius, min_radius, max_radius) : default_radius;
            }
            if (checked && has_image)
                ++enabled;
        }

        if (selected && command != Command::Read) {
            if (command == Command::Apply)
                linux_desktop::save_autostart(linux_desktop::Desktop::Plasma, requested.login);
            return call(Command::Read, {});
        }
        if (selected)
            return {first, "Plasma displays using OpenOLEDShifter: " + std::to_string(selected) +
                "\nPlasma displays configured for shifting: " + std::to_string(enabled), {}};

        throw std::runtime_error(
            "The OpenOLEDShifter wallpaper plugin is installed.\n"
            "Right-click the Plasma desktop and open Configure Desktop and Wallpaper.\n"
            "Select OpenOLEDShifter as the wallpaper type, check the image or pick a local file, enable pixel shifting and click Apply.\n"
            "If the plugin is missing from the list, close and reopen the wallpaper settings.");
    }
};
}

Backend& linux_desktop::plasma_backend() {
    static PlasmaBackend backend;
    return backend;
}
}
