// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include "settings_backend.hpp"
#include <deque>
#include <exception>
#include <future>
#include <optional>

namespace oled {
struct CommandRequest {
    Command command = Command::Read;
    bool from_tray = false;
    bool close_after = false;
};

struct CommandResult {
    CommandRequest request;
    State state;
    std::exception_ptr error;
};

// Called on the UI thread. The backend must outlive this object and its worker.
class SettingsCommands {
public:
    explicit SettingsCommands(Backend& backend) : backend_(backend) {}

    bool busy() const { return pending_.valid(); }
    const CommandRequest& current() const { return current_; }
    void enqueue_tray(Command command, bool close_after);
    std::optional<CommandRequest> next_tray_command();
    std::optional<CommandRequest> next_command(Schedule::Clock::time_point now);

    // Synchronous backends return immediately; worker results are consumed by poll().
    std::optional<CommandResult> begin(CommandRequest request, const Settings& settings);
    std::optional<CommandResult> poll();
    void completed(const CommandRequest& request, Schedule::Clock::time_point now);

private:
    Backend& backend_;
    CommandRequest current_;
    std::future<CommandResult> pending_;
    std::deque<CommandRequest> tray_commands_;
    Schedule::Clock::time_point next_read_{};
};
}
