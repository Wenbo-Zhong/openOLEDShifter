// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "linux_backend.hpp"

#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>

namespace oled::linux_desktop {
namespace {
constexpr char application_id[] = "org.openoledshifter.Desktop.desktop";
constexpr char startup_marker[] = "X-OpenOLEDShifter-Managed";
constexpr char plugin_marker[] = ".openoledshifter-managed";
namespace fs = std::filesystem;

const char* startup_filename(Desktop desktop) {
    return desktop == Desktop::Plasma ? "org.openoledshifter.Desktop-kde.desktop" : application_id;
}

Desktop current_desktop() {
    const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
    if (desktop) {
        g_auto(GStrv) names = g_strsplit(desktop, ":", -1);
        for (auto name = names; *name; ++name)
            if (g_ascii_strcasecmp(*name, "KDE") == 0)
                return Desktop::Plasma;
    }
    return Desktop::Gnome;
}
}

bool plugin_installed(const char* directory, const char* id) {
    const auto installed_in = [&](const char* data_directory) {
        g_autofree char* path = g_build_filename(data_directory, directory, id, "metadata.json", nullptr);
        return g_file_test(path, G_FILE_TEST_IS_REGULAR);
    };
    if (installed_in(g_get_user_data_dir()))
        return true;
    for (auto directories = g_get_system_data_dirs(); *directories; ++directories)
        if (installed_in(*directories))
            return true;
    return false;
}

bool install_bundled_plugin(Desktop desktop, const char* id) {
    const bool plasma = desktop == Desktop::Plasma;
    g_autofree char* executable = g_file_read_link("/proc/self/exe", nullptr);
    constexpr char missing[] = "The bundled plugin is missing. Reinstall the OpenOLEDShifter package.";
    constexpr char failure[] = "Could not install the plugin in your user data directory.";
    if (!executable)
        throw std::runtime_error(missing);
    const auto source = fs::path(executable).parent_path() / "plugins" / (plasma ? "plasma" : "gnome") / id;
    g_autofree char* version = nullptr;
    if (!g_file_get_contents((source / plugin_marker).c_str(), &version, nullptr, nullptr))
        throw std::runtime_error(missing);

    const auto parent = fs::path(g_get_user_data_dir()) / (plasma ? "plasma/wallpapers" : "gnome-shell/extensions");
    const auto destination = parent / id;
    bool updating = false;
    try {
        fs::create_directories(parent);
        const auto status = fs::symlink_status(destination);
        updating = fs::exists(status);
        if (updating) {
            g_autofree char* installed = nullptr;
            const auto marker = destination / plugin_marker;
            if (!fs::is_directory(status) || !fs::is_regular_file(fs::symlink_status(marker)) ||
                !g_file_get_contents(marker.c_str(), &installed, nullptr, nullptr))
                throw std::runtime_error(std::string("Move the existing plugin folder aside, then click the install button again.") +
                    "\n" + destination.string());
            if (g_str_equal(installed, version) && fs::is_regular_file(destination / "metadata.json"))
                return false;
        }
    } catch (const fs::filesystem_error& error) {
        g_debug("%s", error.what());
        throw std::runtime_error(failure);
    }

    // 暂存与目标位于同一文件系统，更新时原子交换完整目录，保留原有目录直到新副本就绪。
    auto pattern = (parent / ".openoledshifter-install-XXXXXX").string();
    if (!g_mkdtemp(pattern.data()))
        throw std::runtime_error(failure);
    const auto staged = fs::path(pattern) / id;
    try {
        fs::copy(source, staged, fs::copy_options::recursive);
        if (renameat2(AT_FDCWD, staged.c_str(), AT_FDCWD, destination.c_str(),
                      updating ? RENAME_EXCHANGE : RENAME_NOREPLACE) != 0)
            throw std::system_error(errno, std::generic_category());
    } catch (const std::system_error& error) {
        std::error_code ignored;
        fs::remove_all(pattern, ignored);
        g_debug("%s", error.what());
        throw std::runtime_error(failure);
    }
    std::error_code cleanup_error;
    fs::remove_all(pattern, cleanup_error);
    if (cleanup_error)
        g_debug("Could not clean up the plugin staging directory: %s", cleanup_error.message().c_str());
    return true;
}

bool autostart_enabled(Desktop desktop) {
    g_autofree char* path = g_build_filename(g_get_user_config_dir(), "autostart", startup_filename(desktop), nullptr);
    g_autoptr(GKeyFile) entry = g_key_file_new();
    return g_key_file_load_from_file(entry, path, G_KEY_FILE_NONE, nullptr) &&
        g_key_file_get_boolean(entry, G_KEY_FILE_DESKTOP_GROUP, startup_marker, nullptr) &&
        !g_key_file_get_boolean(entry, G_KEY_FILE_DESKTOP_GROUP, "Hidden", nullptr);
}

void save_autostart(Desktop desktop, bool enabled) {
    g_autofree char* directory = g_build_filename(g_get_user_config_dir(), "autostart", nullptr);
    g_autofree char* path = g_build_filename(directory, startup_filename(desktop), nullptr);
    constexpr char failure[] = "Could not update OpenOLEDShifter login startup.";
    g_autoptr(GKeyFile) entry = g_key_file_new();
    const bool exists = g_file_test(path, G_FILE_TEST_EXISTS);
    // 只覆盖或删除带本程序所有权标记的启动项；符号链接和用户自建的同名文件不接管。
    if (g_file_test(path, G_FILE_TEST_IS_SYMLINK) ||
        (exists && (!g_key_file_load_from_file(entry, path, G_KEY_FILE_NONE, nullptr) ||
                    !g_key_file_get_boolean(entry, G_KEY_FILE_DESKTOP_GROUP, startup_marker, nullptr))))
        throw std::runtime_error(failure);
    if (!enabled) {
        if (exists && g_unlink(path) != 0 && errno != ENOENT)
            throw std::runtime_error(failure);
        return;
    }

    g_autoptr(GDesktopAppInfo) app = g_desktop_app_info_new(application_id);
    g_autofree char* exec = app ? g_desktop_app_info_get_string(app, "Exec") : nullptr;
    if (!exec || g_mkdir_with_parents(directory, 0700) != 0)
        throw std::runtime_error(failure);
    const auto group = G_KEY_FILE_DESKTOP_GROUP;
    g_key_file_set_string(entry, group, "Type", "Application");
    g_key_file_set_string(entry, group, "Name", "OpenOLEDShifter");
    g_key_file_set_string(entry, group, "Exec", (std::string(exec) + " --background").c_str());
    g_key_file_set_string(entry, group, "OnlyShowIn", desktop == Desktop::Plasma ? "KDE;" : "GNOME;");
    g_key_file_set_string(entry, group, "TryExec", g_app_info_get_executable(G_APP_INFO(app)));
    g_key_file_set_boolean(entry, group, startup_marker, TRUE);
    gsize size = 0;
    g_autofree char* serialized = g_key_file_to_data(entry, &size, nullptr);
    // 使用临时文件替换提交完整内容，避免异常退出留下不完整的登录启动项。
    if (!g_file_set_contents_full(path, serialized, size, G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr))
        throw std::runtime_error(failure);
}
}

namespace oled {
Backend& desktop_backend() {
    return linux_desktop::current_desktop() == linux_desktop::Desktop::Plasma
        ? linux_desktop::plasma_backend() : linux_desktop::gnome_backend();
}
}
