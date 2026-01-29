#include <common/align.h>
#include <common/allocator_linear.h>
#include <common/math.h>

namespace cc
{
    allocator_linear::allocator_linear(void* const buffer, size_t const size, allocator& parent)
        : allocator(buffer, size, parent)
    {
    }

    allocator_linear::allocator_linear(size_t const size, align_val_t const align, allocator& parent)
        : allocator(size, align, parent)
    {
    }

    allocator_linear::allocator_linear(size_t const size, align_val_t const align)
        : allocator(size, align, allocator_top())
    {
    }

    void* allocator_linear::internal_reallocate(void* const ptr, size_t const size, cc::align_val_t const align) noexcept
    {
        if (size == 0 && nullptr != ptr)
            return nullptr;

        size_t const nextPos = (m_used + (align - 1)) & ~(align - 1);

        if (m_pages->head + nextPos + size > m_pages->tail)
            return nullptr;

        uintptr_t const mem = m_pages->head + nextPos;
        m_used = nextPos + size;

        return reinterpret_cast<void*>(mem);
    }

    size_t allocator_linear::internal_size(void const*) const noexcept
    {
        return 0;
    }

    size_t allocator_linear::internal_used() const noexcept
    {
        return m_used;
    }

    allocator_linear_dynamic::allocator_linear_dynamic(size_t const size, align_val_t const align)
        : allocator_linear(size, align)
    {
    }

    allocator_linear_dynamic::allocator_linear_dynamic(size_t const size, align_val_t const align, allocator& alloc)
        : allocator_linear(size, align, alloc)
    {
    }

    allocator_linear_fixed::allocator_linear_fixed(void* const buffer, size_t const size, allocator& alloc)
        : allocator_linear(buffer, size, alloc)
    {
    }
} // namespace cc
