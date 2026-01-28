#pragma once

#include <common/linear_allocator.h>

namespace cc
{
    template<size_t Size>
    linear_allocator_static<Size>::linear_allocator_static(allocator& parent)
        : linear_allocator(m_buffer, Size, parent)
    {
    }
} // namespace cc
