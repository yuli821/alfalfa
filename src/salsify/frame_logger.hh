#pragma once
#include <map>
#include <utility>
#include <cstdint>

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using time_range = std::pair<TimePoint, TimePoint>;

extern std::map<uint32_t, time_range> frame_timestamps;

void dump_frame_timings() {
    std::cout << "Frame timings (ID: duration in ms):\n";
    for (const auto& [frame_id, times] : frame_timestamps) {
        auto start = times.first;
        auto end = times.second;

        // Defensive check in case one of the timestamps wasn't set
        if (end < start) {
            std::cout << "Frame " << frame_id << ": invalid timing\n";
            continue;
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << "Frame " << frame_id << ": " << duration << " ms\n";
    }
}
