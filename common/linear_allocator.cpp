#include <common/align.h>
#include <common/linear_allocator.h>
#include <common/math.h>

namespace cc
{
    linear_allocator::linear_allocator(void* const buffer, size_t const size, allocator& parent)
        : allocator(buffer, size, parent)
    {
    }

    linear_allocator::linear_allocator(size_t const size, align_val_t const align, allocator& parent)
        : allocator(size, align, parent)
    {
    }

    linear_allocator::linear_allocator(size_t const size, align_val_t const align)
        : allocator(size, align, allocator_top())
    {
    }

    void* linear_allocator::internal_reallocate(void* const ptr, size_t const size, cc::align_val_t const align) noexcept
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

    size_t linear_allocator::internal_size(void const*) const noexcept
    {
        return 0;
    }

    size_t linear_allocator::internal_used() const noexcept
    {
        return m_used;
    }

    linear_allocator_dynamic::linear_allocator_dynamic(size_t const size, align_val_t const align)
        : linear_allocator(size, align)
    {
    }

    linear_allocator_dynamic::linear_allocator_dynamic(size_t const size, align_val_t const align, allocator& alloc)
        : linear_allocator(size, align, alloc)
    {
    }

    linear_allocator_fixed::linear_allocator_fixed(void* const buffer, size_t const size, allocator& alloc)
        : linear_allocator(buffer, size, alloc)
    {
    }
} // namespace cc
