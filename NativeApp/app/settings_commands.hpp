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
    // 托盘请求与窗口内操作使用不同的编辑合并规则，结果中需要保留这个来源。
    bool from_tray = false;
    // 处理完该命令的结果后退出，用于“退出并还原”流程。
    bool close_after = false;
};

struct CommandResult {
    CommandRequest request;
    State state;
    std::exception_ptr error;
};

// 由 UI 线程调用；后端的生命周期必须覆盖本对象及其工作线程。
class SettingsCommands {
public:
    explicit SettingsCommands(Backend& backend) : backend_(backend) {}

    bool busy() const { return pending_.valid(); }
    const CommandRequest& current() const { return current_; }
    void enqueue_tray(Command command, bool close_after);
    std::optional<CommandRequest> next_tray_command();
    std::optional<CommandRequest> next_command(Schedule::Clock::time_point now);

    // 同步后端立即返回结果；使用工作线程的后端由 UI 定时调用 poll() 接收结果。
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
