// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "settings_commands.hpp"

namespace oled {
namespace {
CommandResult execute(Backend& backend, CommandRequest request, const Settings& settings) {
    try {
        return {request, backend.call(request.command, settings), {}};
    } catch (const std::exception&) {
        // 将异常随结果带回 UI 线程，由窗口统一展示错误并更新控件状态。
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
    // 用户主动发出的托盘请求优先于周期性状态读取。
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
        // 按值捕获请求和设置快照；工作线程只访问后端，不读取或修改 UI 控件。
        pending_ = std::async(std::launch::async, [backend = &backend_, request, settings] {
            return execute(*backend, request, settings);
        });
        return std::nullopt;
    }
    return execute(backend_, request, settings);
}

std::optional<CommandResult> SettingsCommands::poll() {
    // 零等待只检查是否完成，避免 UI 定时器回调阻塞；get() 取走结果后不再处于忙碌状态。
    if (!busy() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return pending_.get();
}

void SettingsCommands::completed(const CommandRequest& request, Schedule::Clock::time_point now) {
    // 每次命令完成后延迟三秒再读状态；退出请求完成后丢弃剩余托盘请求。
    next_read_ = now + std::chrono::seconds(3);
    if (request.close_after)
        tray_commands_.clear();
}
}
