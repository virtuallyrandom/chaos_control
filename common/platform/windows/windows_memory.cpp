#include <common/assert.h>
#include <common/platform/platform_memory.h>
#include <common/platform/windows.h>

#include <malloc.h>

namespace cc
{
    root_allocator::root_allocator()
        : allocator(nullptr, 0, allocator_root())
    {
    }

    root_allocator::~root_allocator()
    {
    }

    void* root_allocator::internal_reallocate(void* const old, size_t const size, cc::align_val_t const align) noexcept
    {
        if (old)
            m_used -= _aligned_msize(old, 1, 0);

        void* const ptr = _aligned_realloc(old, size, align.as<size_t>());

        if (nullptr != ptr)
            m_used += _aligned_msize(ptr, align.as<size_t>(), 0);

        return ptr;
    }

    size_t root_allocator::internal_size(void const* ptr) const noexcept
    {
        return _aligned_msize(const_cast<void*>(ptr), 1, 0);
    }

    size_t root_allocator::internal_used() const noexcept
    {
        return m_used;
    }
} // namespace cc
