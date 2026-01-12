#pragma once

#include <chrono>

namespace cc
{
    using std::chrono::days;
    using std::chrono::hours;
    using std::chrono::minutes;
    using std::chrono::seconds;
    using std::chrono::milliseconds;
    using std::chrono::microseconds;

    using std::chrono::steady_clock;
    using std::chrono::system_clock;
    using std::chrono::high_resolution_clock;

    using std::chrono::time_point;

    using std::chrono::local_time;
    using local_time_duration = std::chrono::local_time<std::chrono::steady_clock::duration>;

    using std::chrono::current_zone;

    using std::chrono::duration;
    using std::chrono::duration_cast;
} // namespace cc
