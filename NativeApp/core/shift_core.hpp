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
    // 相对稳定原点的物理像素偏移；桌面后端负责换算逻辑坐标，不能叠加到上一次位置。
    int x, y;
    bool operator==(const Offset&) const = default;
};

struct Frame {
    Offset offset;
    int point_index;
    // 往返周期中的下一相位，不是螺旋点序号；同一坐标在去程和回程中有不同相位。
    int next_phase;
};

// 轨迹与默认值是各平台的共同来源；Linux 插件数据由 tools/generate_policy.cpp 生成。
inline std::vector<Frame> make_cycle(int radius) {
    if (radius < min_radius || radius > max_radius)
        throw std::out_of_range("shift radius");

    constexpr std::array directions{Offset{1, 0}, Offset{0, 1}, Offset{-1, 0}, Offset{0, -1}};
    const int side = 2 * radius + 1;
    const int count = side * side;
    std::vector<Offset> points{{0, 0}};
    Offset point{0, 0};
    int direction = 0;
    // 每个步长走两条边后再增长，直到填满半径范围内的方形螺旋。
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

    // 端点只出现一次：到达端点即反向，避免端点重复导致多等待一次位移间隔。
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

// changed 表示暂停原因变化，was_blocked 表示上次处于暂停；force 也不能绕过暂停条件。
constexpr Decision transition(bool blocked, bool changed, bool was_blocked, bool due, bool force) {
    const bool restore = blocked && changed;
    const bool shift = !blocked && (force || (due && !was_blocked));
    return {restore, shift, restore || shift || (!blocked && was_blocked)};
}

class Schedule {
public:
    // 使用单调时钟，避免系统时间校正改变位移间隔。
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
        // 解除暂停后重新等待完整间隔，不补执行暂停期间错过的位移。
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
