// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_backend.hpp"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <memory>

namespace oled {
namespace {
[[noreturn]] void fail(GError* error) {
    std::unique_ptr<GError, decltype(&g_error_free)> owned(error, g_error_free);
    throw std::runtime_error(error ? error->message : "No response from GNOME Shell");
}

JsonNode* member(JsonObject* object, const char* key, GType type) {
    auto* value = json_object_get_member(object, key);
    if (!value || !JSON_NODE_HOLDS_VALUE(value) || json_node_get_value_type(value) != type)
        throw std::runtime_error(std::string("Invalid GNOME extension response: ") + key);
    return value;
}

class GnomeBackend final : public Backend {
    const char* description() const override {
        return "GNOME Wayland: one display with integer scaling and a static wallpaper.";
    }

    const char* failure_hint() const override {
        return "Enable the OpenOLEDShifter GNOME extension.";
    }

    State call(Command command, const Settings& settings) override {
        GError* error = nullptr;
        std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> connection(
            g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error), g_object_unref);
        if (!connection)
            fail(error);
        const char* method = nullptr;
        GVariant* parameters = nullptr;
        switch (command) {
        case Command::SetStartup: throw std::invalid_argument("Use Apply to change GNOME startup settings");
        case Command::Read: method = "GetState"; break;
        case Command::Pause: method = "Pause"; break;
        case Command::Shift: method = "Shift"; break;
        case Command::Apply:
            method = "Apply";
            parameters = g_variant_new("(iibb)", settings.interval, settings.radius,
                                       settings.enabled, settings.login);
            break;
        }
        std::unique_ptr<GVariant, decltype(&g_variant_unref)> reply(
            g_dbus_connection_call_sync(connection.get(), "org.gnome.Shell",
                "/org/openoledshifter/Desktop", "org.openoledshifter.Desktop", method,
                parameters, G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
                2000, nullptr, &error), g_variant_unref);
        if (!reply)
            fail(error);
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
        return {{static_cast<int>(interval), static_cast<int>(radius),
                 json_node_get_boolean(member(fields, "Enabled", G_TYPE_BOOLEAN)) != 0,
                 json_node_get_boolean(member(fields, "StartAtLogin", G_TYPE_BOOLEAN)) != 0},
                json_node_get_string(member(object, "Status", G_TYPE_STRING)), {}};
    }
};
}

Backend& desktop_backend() {
    static GnomeBackend backend;
    return backend;
}
}
