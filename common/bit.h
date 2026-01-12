#pragma once

#include <bit>
#include <bitset>

namespace cc
{
    // count number of 1 bits
    using std::popcount;

    // determine if a value is a power of two
    using std::has_single_bit;

    // obtain smallest power of two that can contain the value
    using std::bit_ceil;

    // count number of zero bits from lsb
    using std::countr_zero;
} // namespace cc
