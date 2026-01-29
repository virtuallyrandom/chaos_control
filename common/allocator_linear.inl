#pragma once

#include <common/allocator_linear.h>

namespace cc
{
    template<size_t Size>
    allocator_linear_static<Size>::allocator_linear_static(allocator& parent)
        : allocator_linear(m_buffer, Size, parent)
    {
    }
} // namespace cc
