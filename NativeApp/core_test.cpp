// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "shift_core.hpp"

#include <cstdlib>
#include <iostream>
#include <set>

void require(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}

int main() try {
    using namespace oled;
    using namespace std::chrono_literals;
    const std::array expected{Offset{0, 0}, Offset{1, 0}, Offset{1, 1}, Offset{0, 1},
                              Offset{-1, 1}, Offset{-1, 0}, Offset{-1, -1}, Offset{0, -1}, Offset{1, -1}};
    const auto defaults = make_cycle(default_radius);
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(defaults[i].offset == expected[i], "default spiral changed");

    for (int radius = min_radius; radius <= max_radius; ++radius) {
        const auto frames = make_cycle(radius);
        const int side = 2 * radius + 1;
        require(frames.size() == 2u * (side * side - 1), "cycle period changed");
        std::set<std::pair<int, int>> points;
        int phase = 0;
        for (std::size_t step = 0; step < frames.size(); ++step) {
            const auto& frame = frames.at(phase);
            const auto& next = frames.at(frame.next_phase);
            require(std::abs(frame.offset.x) <= radius && std::abs(frame.offset.y) <= radius, "unbounded shift");
            require(std::abs(frame.offset.x - next.offset.x) + std::abs(frame.offset.y - next.offset.y) == 1,
                    "non-adjacent frame");
            const int index = phase < side * side ? phase : static_cast<int>(frames.size()) - phase;
            require(frame.point_index == index, "endpoint reversal changed");
            points.emplace(frame.offset.x, frame.offset.y);
            phase = frame.next_phase;
        }
        require(phase == 0 && points.size() == static_cast<std::size_t>(side * side), "incomplete cycle");
    }

    const Schedule::Clock::time_point start{};
    Schedule schedule(default_interval, start);
    require(!schedule.poll(start + 199s, "").shift, "shifted early");
    require(schedule.poll(start + 200s, "").shift, "missed deadline");
    const auto locked = schedule.poll(start + 201s, "locked", true);
    require(locked.restore && !locked.shift, "forced shift ignored lock");
    require(!schedule.poll(start + 400s, "locked").restore, "restoration repeated");
    require(schedule.poll(start + 401s, "sleep").restore, "pause reason change missed");
    require(!schedule.poll(start + 1000s, "").shift, "shifted immediately on resume");
    require(schedule.due() == start + 1200s, "resume did not restart the interval");
    require(schedule.poll(start + 1001s, "", true).shift, "manual shift ignored");
    std::cout << "PASS: native trajectory and schedule\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
