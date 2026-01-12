#pragma once

#include <common/assert.h>
#include <common/intrin.h>

#if defined(min)
#error min shouldn't be defined
#endif // defined(min)

#if defined(max)
#error max shouldn't be defined
#endif // defined(max)

namespace cc
{
    template<class TypeA, class TypeB>
    constexpr TypeA min(TypeA const a, TypeB const b) noexcept
    {
        return a < static_cast<TypeA>(b) ? a : static_cast<TypeA>(b);
    }

    template<class TypeA, class TypeB>
    constexpr TypeA max(TypeA const a, TypeB const b) noexcept
    {
        return a > static_cast<TypeA>(b) ? a : static_cast<TypeA>(b);
    }

    template<class Type, class TypeA, class TypeB>
    constexpr Type clamp(Type const value, TypeA const mn, TypeB const mx) noexcept
    {
        return max(static_cast<Type>(mn), min(value, static_cast<Type>(mx)));
    }

    template<typename Type>
    constexpr Type align(Type const value, size_t const alignment)
    {
        return (value + (alignment - 1)) & ~(alignment - 1);
    }

} // namespace cc
