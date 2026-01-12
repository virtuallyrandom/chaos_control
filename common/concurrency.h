#pragma once

#include <common/intrin.h>

#include <new>

namespace cc
{
    using std::hardware_destructive_interference_size;
    using std::hardware_constructive_interference_size;

    inline void yield() noexcept { _mm_pause(); }
    inline void rw_barrier() noexcept { _ReadWriteBarrier(); }
} // namespace cc
