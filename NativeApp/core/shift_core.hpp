// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oled {

inline constexpr int default_interval = 200;
inline constexpr int min_interval = 10;
inline constexpr int max_interval = 86400;
inline constexpr int default_radius = 1;
inline constexpr int min_radius = 1;
inline constexpr int max_radius = 8;

struct Offset {
    int x, y;
    bool operator==(const Offset&) const = default;
};

struct Frame {
    Offset offset;
    int point_index;
    int next_phase;
};

inline std::vector<Frame> make_cycle(int radius) {
    if (radius < min_radius || radius > max_radius)
        throw std::out_of_range("shift radius");

    constexpr std::array directions{Offset{1, 0}, Offset{0, 1}, Offset{-1, 0}, Offset{0, -1}};
    const int side = 2 * radius + 1;
    const int count = side * side;
    std::vector<Offset> points{{0, 0}};
    Offset point{0, 0};
    int direction = 0;
    for (int length = 1; static_cast<int>(points.size()) < count; ++length) {
        for (int leg = 0; leg < 2; ++leg) {
            const auto delta = directions[direction++ % directions.size()];
            for (int step = 0; step < length && static_cast<int>(points.size()) < count; ++step) {
                point.x += delta.x;
                point.y += delta.y;
                points.push_back(point);
            }
        }
    }

    const int period = 2 * (count - 1);
    std::vector<Frame> frames;
    frames.reserve(period);
    for (int phase = 0; phase < period; ++phase) {
        const int index = phase < count ? phase : period - phase;
        frames.push_back({points[index], index, (phase + 1) % period});
    }
    return frames;
}

struct Decision {
    bool restore;
    bool shift;
    bool reset_deadline;
};

constexpr Decision transition(bool blocked, bool changed, bool was_blocked, bool due, bool force) {
    const bool restore = blocked && changed;
    const bool shift = !blocked && (force || (due && !was_blocked));
    return {restore, shift, restore || shift || (!blocked && was_blocked)};
}

class Schedule {
public:
    using Clock = std::chrono::steady_clock;

    Schedule(int seconds, Clock::time_point now) { reset(seconds, now); }

    void reset(int seconds, Clock::time_point now) {
        if (seconds < min_interval || seconds > max_interval)
            throw std::out_of_range("shift interval");
        interval_ = std::chrono::seconds(seconds);
        pause_.clear();
        due_ = now + interval_;
    }

    Decision poll(Clock::time_point now, std::string_view pause, bool force = false) {
        const auto next = transition(!pause.empty(), pause != pause_, !pause_.empty(), now >= due_, force);
        pause_ = pause;
        if (next.reset_deadline)
            due_ = now + interval_;
        return next;
    }

    Clock::time_point due() const { return due_; }

private:
    std::chrono::seconds interval_{};
    Clock::time_point due_{};
    std::string pause_;
};

} // namespace oled
