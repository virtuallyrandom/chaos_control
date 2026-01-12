#pragma once

#include <chrono>
#include <ratio>
#include <cstdint>

namespace cc
{
    using std::chrono::nanoseconds;
    using std::chrono::microseconds;
    using std::chrono::milliseconds;
    using std::chrono::seconds;
    using std::chrono::minutes;
    using std::chrono::hours;
    using days = std::chrono::duration<int, std::ratio<86400>>;
    using weeks = std::chrono::duration<int, std::ratio_multiply<std::ratio<7>, std::chrono::days::period>>;
    using years = std::chrono::duration<int, std::ratio_multiply<std::ratio<146097, 400>, days::period>>;
    using months = std::chrono::duration<int, std::ratio_divide<years::period, std::ratio<12>>>;

    using std::chrono::duration;

    using std::chrono::duration_cast;

    using std::chrono::system_clock;
    using std::chrono::steady_clock;
    using std::chrono::high_resolution_clock;

    class ScopeElapsed
    {
    public:
        ScopeElapsed(steady_clock::duration* const);
        ScopeElapsed();
        ~ScopeElapsed();

        steady_clock::duration current() const;

    private:
        steady_clock::duration* m_duration = nullptr;
        steady_clock::time_point m_start;
    };

    void toDuration(steady_clock::duration const, float* const value, char const** const unit);
} // namespace cc

#include <common/time.inl>