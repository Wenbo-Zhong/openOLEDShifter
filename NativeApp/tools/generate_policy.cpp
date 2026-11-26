// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "core/shift_core.hpp"
#include "core/localization.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <utility>

int main(int argc, char** argv) try {
    using namespace oled;
    if (argc != 4)
        throw std::runtime_error("Usage: generate_policy schema.in schema.xml cycles.js");
    std::ifstream input(argv[1]);
    if (!input)
        throw std::runtime_error("Cannot open schema template");
    std::string schema{std::istreambuf_iterator<char>(input), {}};
    for (const auto& [key, value] : {std::pair{"@DEFAULT_INTERVAL@", default_interval},
             {"@MIN_INTERVAL@", min_interval}, {"@MAX_INTERVAL@", max_interval},
             {"@DEFAULT_RADIUS@", default_radius}, {"@MIN_RADIUS@", min_radius}, {"@MAX_RADIUS@", max_radius}}) {
        auto position = schema.find(key);
        if (position == std::string::npos)
            throw std::runtime_error(std::string("Missing schema token: ") + key);
        const auto replacement = std::to_string(value);
        while (position != std::string::npos) {
            schema.replace(position, std::char_traits<char>::length(key), replacement);
            position = schema.find(key, position + replacement.size());
        }
    }
    std::ofstream schema_file;
    schema_file.exceptions(std::ios::failbit | std::ios::badbit);
    schema_file.open(argv[2], std::ios::binary);
    schema_file << schema;
    schema_file.close();

    // 插件读取生成数据而不另写轨迹/调度规则；修改策略时应修改共享核心并重新生成。
    std::ofstream js;
    js.exceptions(std::ios::failbit | std::ios::badbit);
    js.open(argv[3], std::ios::binary);
    js << "// SPDX-License-Identifier: GPL-3.0-only\n"
       << "export const defaults = {interval:" << default_interval << ",radius:" << default_radius
       << ",minInterval:" << min_interval << ",maxInterval:" << max_interval
       << ",minRadius:" << min_radius << ",maxRadius:" << max_radius << "};\n"
       << "export const cycles = {";
    for (int radius = min_radius; radius <= max_radius; ++radius) {
        if (radius != min_radius)
            js << ',';
        js << '"' << radius << "\":[";
        const auto frames = make_cycle(radius);
        for (std::size_t phase = 0; phase < frames.size(); ++phase) {
            if (phase != 0)
                js << ',';
            const auto& frame = frames[phase];
            // 数组契约为 [物理像素 x, 物理像素 y, 下一相位]，与 Linux/shared/shift-runtime.js 对应。
            js << '[' << frame.offset.x << ',' << frame.offset.y << ',' << frame.next_phase << ']';
        }
        js << ']';
    }
    js << "};\nexport const scheduleTransitions = [" << std::boolalpha;
    // 位顺序必须与 JS 运行时一致：暂停、原因变化、上次暂停、到期、强制位移。
    for (int flags = 0; flags < 32; ++flags) {
        if (flags != 0)
            js << ',';
        const auto step = transition(flags & 1, flags & 2, flags & 4, flags & 8, flags & 16);
        js << "{\"Restore\":" << step.restore << ",\"Shift\":" << step.shift
           << ",\"ResetDeadline\":" << step.reset_deadline << '}';
    }
    js << "];\n";
    (void)localization::load();
    js.close();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
