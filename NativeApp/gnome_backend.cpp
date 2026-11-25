// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_backend.hpp"

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <cerrno>
#include <json-glib/json-glib.h>
#include <memory>

namespace oled {
namespace {
[[noreturn]] void fail(GError* error) {
    std::unique_ptr<GError, decltype(&g_error_free)> owned(error, g_error_free);
    if (error)
        g_debug("OpenOLEDShifter GNOME backend: %s", error->message);
    throw std::runtime_error("");
}

constexpr char extension_id[] = "openoledshifter@openoledshifter";

bool extension_installed() {
    const auto installed_in = [](const char* directory) {
        std::unique_ptr<char, decltype(&g_free)> metadata(
            g_build_filename(directory, "gnome-shell", "extensions", extension_id, "metadata.json", nullptr), g_free);
        return g_file_test(metadata.get(), G_FILE_TEST_IS_REGULAR);
    };
    if (installed_in(g_get_user_data_dir()))
        return true;
    for (auto directories = g_get_system_data_dirs(); *directories; ++directories)
        if (installed_in(*directories))
            return true;
    return false;
}

[[noreturn]] void explain_unavailable(GDBusConnection* connection, GError* error) {
    std::unique_ptr<GError, decltype(&g_error_free)> owned(error, g_error_free);
    if (error)
        g_debug("OpenOLEDShifter GNOME backend: %s", error->message);

    std::unique_ptr<GVariant, decltype(&g_variant_unref)> info(
        g_dbus_connection_call_sync(connection, "org.gnome.Shell.Extensions",
            "/org/gnome/Shell/Extensions", "org.gnome.Shell.Extensions", "GetExtensionInfo",
            g_variant_new("(s)", extension_id), G_VARIANT_TYPE("(a{sv})"),
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr), g_variant_unref);
    if (!info)
        throw std::runtime_error("GNOME Shell is unavailable. Open the app from an Ubuntu GNOME Wayland session.");

    std::unique_ptr<GVariant, decltype(&g_variant_unref)> fields(g_variant_get_child_value(info.get(), 0),
        g_variant_unref);
    if (g_variant_n_children(fields.get()) == 0) {
        if (!extension_installed())
            throw std::runtime_error("Wallpaper extension files are missing. Reinstall the OpenOLEDShifter package.");
        throw std::runtime_error(
            "The wallpaper extension is installed but not loaded in this session. Save your work, sign out, and sign back in.\n"
            "Then run this command as your desktop user and reopen OpenOLEDShifter.\n"
            "gnome-extensions enable openoledshifter@openoledshifter");
    }

    gboolean enabled = FALSE;
    if (g_variant_lookup(fields.get(), "enabled", "b", &enabled) && enabled)
        throw std::runtime_error(
            "The wallpaper extension is enabled but not responding. Sign out and back in to load the installed version.");

    throw std::runtime_error(
        "The wallpaper extension is disabled. Run this command as your desktop user.\n"
        "gnome-extensions enable openoledshifter@openoledshifter");
}

constexpr char startup_marker[] = "X-OpenOLEDShifter-Managed";
constexpr char kde_startup[] = "org.openoledshifter.Desktop-kde.desktop";

bool kde_autostart_enabled() {
    std::unique_ptr<char, decltype(&g_free)> path(
        g_build_filename(g_get_user_config_dir(), "autostart", kde_startup, nullptr), g_free);
    std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> entry(g_key_file_new(), g_key_file_unref);
    return g_key_file_load_from_file(entry.get(), path.get(), G_KEY_FILE_NONE, nullptr) &&
        g_key_file_get_boolean(entry.get(), G_KEY_FILE_DESKTOP_GROUP, startup_marker, nullptr) &&
        !g_key_file_get_boolean(entry.get(), G_KEY_FILE_DESKTOP_GROUP, "Hidden", nullptr);
}

void save_autostart(bool enabled, bool kde = false) {
    constexpr char id[] = "org.openoledshifter.Desktop.desktop";
    std::unique_ptr<char, decltype(&g_free)> directory(
        g_build_filename(g_get_user_config_dir(), "autostart", nullptr), g_free);
    std::unique_ptr<char, decltype(&g_free)> path(g_build_filename(directory.get(), kde ? kde_startup : id, nullptr), g_free);
    constexpr char failure[] = "Could not update OpenOLEDShifter login startup.";
    std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> entry(g_key_file_new(), g_key_file_unref);
    const bool exists = g_file_test(path.get(), G_FILE_TEST_EXISTS);
    if (g_file_test(path.get(), G_FILE_TEST_IS_SYMLINK) ||
        (exists && (!g_key_file_load_from_file(entry.get(), path.get(), G_KEY_FILE_NONE, nullptr) ||
                    !g_key_file_get_boolean(entry.get(), G_KEY_FILE_DESKTOP_GROUP, startup_marker, nullptr))))
        throw std::runtime_error(failure);
    if (!enabled) {
        if (exists && g_unlink(path.get()) != 0 && errno != ENOENT)
            throw std::runtime_error(failure);
        return;
    }

    std::unique_ptr<GDesktopAppInfo, decltype(&g_object_unref)> app(g_desktop_app_info_new(id), g_object_unref);
    std::unique_ptr<char, decltype(&g_free)> exec(app ? g_desktop_app_info_get_string(app.get(), "Exec") : nullptr, g_free);
    if (!exec || g_mkdir_with_parents(directory.get(), 0700) != 0)
        throw std::runtime_error(failure);
    const auto group = G_KEY_FILE_DESKTOP_GROUP;
    g_key_file_set_string(entry.get(), group, "Type", "Application");
    g_key_file_set_string(entry.get(), group, "Name", "OpenOLEDShifter");
    g_key_file_set_string(entry.get(), group, "Exec", (std::string(exec.get()) + " --background").c_str());
    g_key_file_set_string(entry.get(), group, "OnlyShowIn", kde ? "KDE;" : "GNOME;");
    g_key_file_set_string(entry.get(), group, "TryExec", g_app_info_get_executable(G_APP_INFO(app.get())));
    g_key_file_set_boolean(entry.get(), group, startup_marker, TRUE);
    gsize size = 0;
    std::unique_ptr<char, decltype(&g_free)> serialized(g_key_file_to_data(entry.get(), &size, nullptr), g_free);
    if (!g_file_set_contents_full(path.get(), serialized.get(), size,
                                  G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr))
        throw std::runtime_error(failure);
}

JsonNode* member(JsonObject* object, const char* key, GType type) {
    auto* value = json_object_get_member(object, key);
    if (!value || !JSON_NODE_HOLDS_VALUE(value) || json_node_get_value_type(value) != type)
        throw std::runtime_error(std::string("Invalid GNOME extension response: ") + key);
    return value;
}

bool plasma_session() {
    const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
    if (!desktop)
        return false;
    std::unique_ptr<char*, decltype(&g_strfreev)> names(g_strsplit(desktop, ":", -1), g_strfreev);
    for (auto name = names.get(); *name; ++name)
        if (g_ascii_strcasecmp(*name, "KDE") == 0)
            return true;
    return false;
}

bool plasma_wallpaper_installed() {
    const auto exists_in = [](const char* directory) {
        std::unique_ptr<char, decltype(&g_free)> path(g_build_filename(directory, "plasma", "wallpapers",
            "org.openoledshifter.wallpaper", "metadata.json", nullptr), g_free);
        return g_file_test(path.get(), G_FILE_TEST_IS_REGULAR);
    };
    if (exists_in(g_get_user_data_dir()))
        return true;
    for (auto directory = g_get_system_data_dirs(); *directory; ++directory)
        if (exists_in(*directory))
            return true;
    return false;
}

class PlasmaBackend final : public Backend {
    const char* description() const override { return "Save applies to all displays already using OpenOLEDShifter. Choose images per screen in Plasma wallpaper settings."; }
    const char* failure_hint() const override { return "Set up the Plasma wallpaper plugin"; }
    DesktopFeatures features() const override { return {.fullscreen_pause = true}; }
    const char* login_label() const override { return "Start KDE tray at login"; }

    State call(Command command, const Settings& requested) override {
        if (command != Command::Read && command != Command::Apply &&
            command != Command::Pause && command != Command::Toggle && command != Command::Shift)
            return {};
        if (!plasma_wallpaper_installed())
            throw std::runtime_error(
                std::string("Use the openoledshifter-plasma.zip package.\n") +
                "Extract the archive into the following folder. Create it if needed.\n"
                + g_get_user_data_dir() + "/plasma/wallpapers/org.openoledshifter.wallpaper\n"
                "Then reopen this window to check the installation.");

        std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> connection(
            g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_object_unref);
        if (!connection)
            throw std::runtime_error("Could not read the current Plasma wallpaper settings.");
        unsigned selected = 0, enabled = 0;
        Settings first;
        first.login = kde_autostart_enabled();
        constexpr char plugin_id[] = "org.openoledshifter.wallpaper";
        // Plasma screen IDs can have gaps after disconnecting a display.
        for (unsigned screen = 0; screen < 16; ++screen) {
            std::unique_ptr<GVariant, decltype(&g_variant_unref)> reply(
                g_dbus_connection_call_sync(connection.get(), "org.kde.plasmashell", "/PlasmaShell",
                    "org.kde.PlasmaShell", "wallpaper", g_variant_new("(u)", screen),
                    G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                    500, nullptr, nullptr), g_variant_unref);
            if (!reply)
                throw std::runtime_error("Could not read the current Plasma wallpaper settings.");
            std::unique_ptr<GVariant, decltype(&g_variant_unref)> fields(
                g_variant_get_child_value(reply.get(), 0), g_variant_unref);
            const char* plugin = nullptr;
            if (!g_variant_lookup(fields.get(), "wallpaperPlugin", "&s", &plugin) ||
                std::string_view(plugin) != plugin_id)
                continue;

            gboolean checked = FALSE;
            const char* image = nullptr;
            gint32 interval = 0, radius = 0, serial = 0;
            gboolean pause_fullscreen = TRUE;
            g_variant_lookup(fields.get(), "PauseFullscreen", "b", &pause_fullscreen);
            g_variant_lookup(fields.get(), "ShiftRequest", "i", &serial);
            g_variant_lookup(fields.get(), "Enabled", "b", &checked);
            g_variant_lookup(fields.get(), "Interval", "i", &interval);
            g_variant_lookup(fields.get(), "Radius", "i", &radius);
            const bool has_image = g_variant_lookup(fields.get(), "Image", "&s", &image) &&
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
                std::unique_ptr<GVariant, decltype(&g_variant_unref)> changed(
                    g_dbus_connection_call_sync(connection.get(), "org.kde.plasmashell", "/PlasmaShell",
                        "org.kde.PlasmaShell", "setWallpaper",
                        g_variant_new("(s@a{sv}u)", plugin_id, g_variant_builder_end(&updates), screen),
                        G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, nullptr),
                    g_variant_unref);
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
                save_autostart(requested.login, true);
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

class GnomeBackend final : public Backend {
    const char* description() const override {
        return "GNOME Wayland: static wallpapers on displays with integer, 125%, or 150% scaling.";
    }

    const char* failure_hint() const override {
        return "Wallpaper shifting is unavailable. The tray still works.";
    }

    const char* setup_label() const override { return "Enable wallpaper extension"; }

    DesktopFeatures features() const override { return {.fullscreen_pause = true}; }

    State call(Command command, const Settings& settings) override {
        GError* error = nullptr;
        std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> connection(
            g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error), g_object_unref);
        if (!connection)
            fail(error);
        const char* method = nullptr;
        GVariant* parameters = nullptr;
        switch (command) {
        case Command::Setup: {
            std::unique_ptr<GVariant, decltype(&g_variant_unref)> enabled(
                g_dbus_connection_call_sync(connection.get(), "org.gnome.Shell.Extensions",
                    "/org/gnome/Shell/Extensions", "org.gnome.Shell.Extensions", "EnableExtension",
                    g_variant_new("(s)", extension_id), G_VARIANT_TYPE("(b)"),
                    G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error), g_variant_unref);
            gboolean accepted = FALSE;
            if (enabled)
                g_variant_get(enabled.get(), "(b)", &accepted);
            if (!accepted)
                explain_unavailable(connection.get(), error);
            method = "GetState";
            break;
        }
        case Command::SetStartup: throw std::invalid_argument("Use Apply to change GNOME startup settings");
        case Command::Read: method = "GetState"; break;
        case Command::Pause: method = "Pause"; break;
        case Command::Shift: method = "Shift"; break;
        case Command::Toggle: method = "Toggle"; break;
        case Command::Apply:
            method = "ApplyOptions";
            parameters = g_variant_new("(iibbb)", settings.interval, settings.radius,
                                       settings.enabled, settings.login, settings.pause_fullscreen);
            break;
        }
        std::unique_ptr<GVariant, decltype(&g_variant_unref)> reply(
            g_dbus_connection_call_sync(connection.get(), "org.gnome.Shell",
                "/org/openoledshifter/Desktop", "org.openoledshifter.Desktop", method,
                parameters, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                2000, nullptr, &error), g_variant_unref);
        if (!reply)
            explain_unavailable(connection.get(), error);
        const char* json = nullptr;
        g_variant_get(reply.get(), "(&s)", &json);
        std::unique_ptr<JsonParser, decltype(&g_object_unref)> parser(json_parser_new(), g_object_unref);
        if (!json_parser_load_from_data(parser.get(), json, -1, &error))
            fail(error);
        auto* root = json_parser_get_root(parser.get());
        if (!root || !JSON_NODE_HOLDS_OBJECT(root))
            throw std::runtime_error("Invalid GNOME extension response");
        auto* object = json_node_get_object(root);
        auto* data = json_object_get_member(object, "Settings");
        if (!data || !JSON_NODE_HOLDS_OBJECT(data))
            throw std::runtime_error("GNOME extension did not return settings");
        auto* fields = json_node_get_object(data);
        const auto interval = json_node_get_int(member(fields, "IntervalSeconds", G_TYPE_INT64));
        const auto radius = json_node_get_int(member(fields, "Radius", G_TYPE_INT64));
        if (interval < min_interval || interval > max_interval || radius < min_radius || radius > max_radius)
            throw std::runtime_error("GNOME extension returned out-of-range settings");
        State result{{static_cast<int>(interval), static_cast<int>(radius),
                 json_node_get_boolean(member(fields, "Enabled", G_TYPE_BOOLEAN)) != 0,
                 json_node_get_boolean(member(fields, "StartAtLogin", G_TYPE_BOOLEAN)) != 0},
                json_node_get_string(member(object, "Status", G_TYPE_STRING)), {}};
        if (json_object_has_member(fields, "PauseFullscreen"))
            result.settings.pause_fullscreen = json_node_get_boolean(member(fields, "PauseFullscreen", G_TYPE_BOOLEAN));
        if (result.status == "Wallpaper shifting is off")
            result.status += "\nSelect Enable timed shifts and click Save to start.";
        if (command == Command::Apply)
            save_autostart(result.settings.login);
        return result;
    }
};
}

Backend& desktop_backend() {
    static GnomeBackend gnome;
    static PlasmaBackend plasma;
    if (plasma_session())
        return plasma;
    return gnome;
}
}
