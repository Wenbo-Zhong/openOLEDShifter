// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "app/settings_commands.hpp"

#include <functional>
#include <iostream>
#include <thread>

namespace {
using namespace oled;
using namespace std::chrono_literals;

void require(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}

class FixtureBackend final : public Backend {
public:
    const char* description() const override { return "Fixture"; }
    const char* failure_hint() const override { return "Fixture error"; }
    bool use_worker() const override { return worker; }
    State call(Command command, const Settings& settings) override {
        if (before_call)
            before_call();
        calls.push_back(command);
        received = settings;
        thread = std::this_thread::get_id();
        if (fail)
            throw std::runtime_error("backend unavailable");
        return {settings, "fixture state", {{"primary", "Fixture display"}}};
    }

    bool worker = false;
    bool fail = false;
    std::function<void()> before_call;
    std::vector<Command> calls;
    Settings received;
    std::thread::id thread;
};

CommandResult await_result(SettingsCommands& commands) {
    const auto deadline = Schedule::Clock::now() + 5s;
    while (Schedule::Clock::now() < deadline) {
        if (auto result = commands.poll())
            return std::move(*result);
        std::this_thread::yield();
    }
    throw std::runtime_error("Worker did not complete");
}

void require_error(const CommandResult& result) {
    require(result.error != nullptr, "backend error was lost");
    try {
        std::rethrow_exception(result.error);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()) == "backend unavailable", "backend error text changed");
    }
}

void synchronous_commands() {
    FixtureBackend backend;
    SettingsCommands commands(backend);
    const Schedule::Clock::time_point now{};
    Settings edits;
    edits.interval = 42;
    edits.window_processes = "fixture.exe";

    const auto first = commands.next_command(now);
    require(first && first->command == Command::Read, "initial settings read missing");
    auto result = commands.begin(*first, edits);
    require(result && !result->error && !commands.busy(), "synchronous command did not finish immediately");
    require(backend.thread == std::this_thread::get_id(), "synchronous backend ran on a worker");
    require(result->state.settings.interval == 42 && backend.received.window_processes == "fixture.exe",
            "settings snapshot changed");
    commands.completed(result->request, now);
    require(!commands.next_command(now + 2999ms), "settings refreshed before three seconds");

    commands.enqueue_tray(Command::Toggle, false);
    commands.enqueue_tray(Command::Shift, false);
    auto request = commands.next_command(now + 3s);
    require(request && request->command == Command::Toggle && request->from_tray && !request->close_after,
            "tray command did not take priority over overdue read");
    result = commands.begin(*request, edits);
    require(result && result->request.from_tray, "tray origin was lost");
    commands.completed(result->request, now + 3s);
    request = commands.next_command(now + 3s);
    require(request && request->command == Command::Shift, "tray command order changed");
    result = commands.begin(*request, edits);
    commands.completed(result->request, now + 3s);
    require(!commands.next_command(now + 5999ms), "completion did not reset refresh deadline");
    request = commands.next_command(now + 6s);
    require(request && request->command == Command::Read, "periodic settings read missing");

    backend.fail = true;
    commands.enqueue_tray(Command::Pause, true);
    commands.enqueue_tray(Command::Toggle, false);
    request = commands.next_tray_command();
    result = commands.begin(*request, edits);
    require_error(*result);
    require(result->request.command == Command::Pause && result->request.from_tray && result->request.close_after,
            "exit metadata was lost on failure");
    commands.completed(result->request, now + 6s);
    require(!commands.next_tray_command(), "commands remained queued after exit");
    require(!commands.poll(), "idle controller produced a worker result");
}

void worker_commands() {
    FixtureBackend backend;
    backend.worker = true;
    std::promise<void> entered, release;
    const auto released = release.get_future().share();
    backend.before_call = [&] {
        entered.set_value();
        released.wait();
    };
    SettingsCommands commands(backend);
    struct ReleaseOnExit {
        std::promise<void>& signal;
        ~ReleaseOnExit() { try { signal.set_value(); } catch (const std::future_error&) {} }
    } cleanup{release};

    Settings edits;
    edits.interval = 57;
    require(!commands.begin({Command::Apply}, edits) && commands.busy(), "worker command was not pending");
    require(entered.get_future().wait_for(5s) == std::future_status::ready, "worker did not enter backend");
    edits.interval = 99;
    commands.enqueue_tray(Command::Pause, true);
    commands.enqueue_tray(Command::Shift, false);
    require(!commands.next_command(Schedule::Clock::now()), "queued command overlapped active worker");
    require(!commands.next_tray_command(), "tray dequeued during active worker");
    require(!commands.begin({Command::Shift}, edits), "second command overlapped active worker");
    require(commands.current().command == Command::Apply, "pending request was replaced");
    require(!commands.poll(), "blocked worker reported completion");

    release.set_value();
    auto result = await_result(commands);
    require(!result.error && !commands.busy(), "worker completion did not release controller");
    require(backend.calls == std::vector{Command::Apply}, "overlapping backend call was dispatched");
    require(result.state.settings.interval == 57, "worker did not retain its settings snapshot");
    require(backend.thread != std::this_thread::get_id(), "worker backend ran on caller thread");
    commands.completed(result.request, Schedule::Clock::now());
    const auto request = commands.next_command(Schedule::Clock::now());
    require(request && request->command == Command::Pause && request->close_after,
            "queued exit request was lost while worker ran");

    backend.before_call = {};
    backend.fail = true;
    require(!commands.begin(*request, edits), "worker error completed synchronously");
    result = await_result(commands);
    require_error(result);
    require(result.request.from_tray && result.request.close_after, "worker error lost exit metadata");
    commands.completed(result.request, Schedule::Clock::now());
    require(!commands.next_tray_command(), "worker exit did not clear subsequent requests");
}
}

int main() try {
    synchronous_commands();
    worker_commands();
    std::cout << "PASS: command serialization, refresh, snapshots, errors and exit cleanup\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
