// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "linux_backend.hpp"

#include <gio/gio.h>
#include <json-glib/json-glib.h>

namespace oled {
namespace {
[[noreturn]] void fail(GError* error) {
    g_autoptr(GError) owned = error;
    if (owned)
        g_debug("OpenOLEDShifter GNOME backend: %s", owned->message);
    throw std::runtime_error("");
}

constexpr char extension_id[] = "openoledshifter@openoledshifter";

bool extension_installed() {
    return linux_desktop::plugin_installed("gnome-shell/extensions", extension_id);
}

[[noreturn]] void explain_unavailable(GDBusConnection* connection, GError* error) {
    g_autoptr(GError) owned = error;
    if (owned)
        g_debug("OpenOLEDShifter GNOME backend: %s", owned->message);

    g_autoptr(GVariant) info = g_dbus_connection_call_sync(connection, "org.gnome.Shell.Extensions",
        "/org/gnome/Shell/Extensions", "org.gnome.Shell.Extensions", "GetExtensionInfo",
        g_variant_new("(s)", extension_id), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    if (!info)
        throw std::runtime_error("GNOME Shell is unavailable. Open the app from an Ubuntu GNOME Wayland session.");

    g_autoptr(GVariant) fields = g_variant_get_child_value(info, 0);
    if (g_variant_n_children(fields) == 0) {
        if (!extension_installed())
            throw std::runtime_error("Wallpaper extension files are missing. Reinstall the OpenOLEDShifter package.");
        throw std::runtime_error(
            "The wallpaper extension is installed but not loaded in this session. Save your work, sign out, and sign back in.\n"
            "After signing back in, open OpenOLEDShifter and click Enable wallpaper extension.");
    }

    gboolean enabled = FALSE;
    if (g_variant_lookup(fields, "enabled", "b", &enabled) && enabled)
        throw std::runtime_error(
            "The wallpaper extension is enabled but not responding. Sign out and back in to load the installed version.");

    throw std::runtime_error(
        "The wallpaper extension is disabled. Run this command as your desktop user.\n"
        "gnome-extensions enable openoledshifter@openoledshifter");
}

// Shell 中加载的扩展版本可能与应用不同；响应必须校验字段类型和设置范围后才能进入共享状态。
JsonNode* member(JsonObject* object, const char* key, GType type) {
    auto* value = json_object_get_member(object, key);
    if (!value || !JSON_NODE_HOLDS_VALUE(value) || json_node_get_value_type(value) != type)
        throw std::runtime_error(std::string("Invalid GNOME extension response: ") + key);
    return value;
}

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
        g_autoptr(GDBusConnection) connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!connection)
            fail(error);
        const char* method = nullptr;
        GVariant* parameters = nullptr;
        switch (command) {
        case Command::Setup: {
            g_autoptr(GVariant) enabled = g_dbus_connection_call_sync(connection, "org.gnome.Shell.Extensions",
                "/org/gnome/Shell/Extensions", "org.gnome.Shell.Extensions", "EnableExtension",
                g_variant_new("(s)", extension_id), G_VARIANT_TYPE("(b)"),
                G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
            gboolean accepted = FALSE;
            if (enabled)
                g_variant_get(enabled, "(b)", &accepted);
            if (!accepted)
                explain_unavailable(connection, error);
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
            // 参数顺序与 Linux/extension/extension.js 的 D-Bus 声明和 ApplyOptions 方法保持一致。
            parameters = g_variant_new("(iibbb)", settings.interval, settings.radius,
                                       settings.enabled, settings.login, settings.pause_fullscreen);
            break;
        }
        g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, "org.gnome.Shell",
            "/org/openoledshifter/Desktop", "org.openoledshifter.Desktop", method,
            parameters, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, nullptr, &error);
        if (!reply)
            explain_unavailable(connection, error);
        const char* json = nullptr;
        g_variant_get(reply, "(&s)", &json);
        g_autoptr(JsonParser) parser = json_parser_new();
        if (!json_parser_load_from_data(parser, json, -1, &error))
            fail(error);
        auto* root = json_parser_get_root(parser);
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
            linux_desktop::save_autostart(linux_desktop::Desktop::Gnome, result.settings.login);
        return result;
    }
};
}

Backend& linux_desktop::gnome_backend() {
    static GnomeBackend backend;
    return backend;
}
}
