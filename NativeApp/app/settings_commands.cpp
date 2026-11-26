// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_commands.hpp"

namespace oled {
namespace {
CommandResult execute(Backend& backend, CommandRequest request, const Settings& settings) {
    try {
        return {request, backend.call(request.command, settings), {}};
    } catch (const std::exception&) {
        return {request, {}, std::current_exception()};
    }
}
}

void SettingsCommands::enqueue_tray(Command command, bool close_after) {
    tray_commands_.push_back({command, true, close_after});
}

std::optional<CommandRequest> SettingsCommands::next_tray_command() {
    if (busy() || tray_commands_.empty())
        return std::nullopt;
    const auto request = tray_commands_.front();
    tray_commands_.pop_front();
    return request;
}

std::optional<CommandRequest> SettingsCommands::next_command(Schedule::Clock::time_point now) {
    if (busy())
        return std::nullopt;
    if (auto request = next_tray_command())
        return request;
    if (now >= next_read_)
        return CommandRequest{};
    return std::nullopt;
}

std::optional<CommandResult> SettingsCommands::begin(CommandRequest request, const Settings& settings) {
    if (busy())
        return std::nullopt;
    current_ = request;
    if (backend_.use_worker()) {
        pending_ = std::async(std::launch::async, [backend = &backend_, request, settings] {
            return execute(*backend, request, settings);
        });
        return std::nullopt;
    }
    return execute(backend_, request, settings);
}

std::optional<CommandResult> SettingsCommands::poll() {
    if (!busy() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return pending_.get();
}

void SettingsCommands::completed(const CommandRequest& request, Schedule::Clock::time_point now) {
    next_read_ = now + std::chrono::seconds(3);
    if (request.close_after)
        tray_commands_.clear();
}
}
