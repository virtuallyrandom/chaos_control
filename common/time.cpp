#include <common/time.h>

#include <common/compiler.h>

namespace cc
{
    void toDuration(steady_clock::duration const duration, double* const value, char const** const unit)
    {
        intmax_t asNum = duration_cast<nanoseconds>(duration).count();

        struct Step
        {
            intmax_t const period;
            char const* const unit;
        } steps[] =
        {
            { years::period::num, "Y" },
            { months::period::num, "M" },
            { weeks::period::num, "W" },
            { days::period::num, "D" },
            { hours::period::num, "h" },
            { minutes::period::num, "m" },
            { seconds::period::num, "s" },
            { milliseconds::period::num, "ms" },
            { microseconds::period::num, "us" },
            { nanoseconds::period::num, "ns" },
        };
        for (size_t i = 0; i < countof(steps); i++)
        {
            Step const& step = steps[i];
            if (step.period > asNum)
            {
                *value = static_cast<double>(asNum) / static_cast<double>(step.period);
                *unit = step.unit;
                break;
            }
        }
        *value = static_cast<double>(asNum);
        *unit = "c";
    }

} // namespace cc
