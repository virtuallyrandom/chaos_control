#pragma once

#include <common/align.h>

namespace cc
{
    template<typename Type>
    Type align_up(Type value, align_val_t align)
    {
        uintptr_t const uv = static_cast<uintptr_t>(value);
        return static_cast<Type>((uv + (align - 1)) & ~(align - 1));
    }

    template<>
    inline uintptr_t align_up(uintptr_t value, align_val_t align)
    {
        return (value + (align - 1)) & ~(align - 1);
    }

    template<typename Type>
    Type align_down(Type value, align_val_t align)
    {
        uintptr_t const uv = static_cast<uintptr_t>(value);
        return static_cast<Type>(uv & ~(align - 1));
    }

    template<>
    inline uintptr_t align_down(uintptr_t value, align_val_t align)
    {
        return value & ~(align - 1);
    }
} // namespace cc
