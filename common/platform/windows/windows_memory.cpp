#include <common/assert.h>
#include <common/platform/platform_memory.h>
#include <common/platform/windows.h>

#include <malloc.h>

namespace cc
{
    root_allocator::root_allocator()
    {
        HANDLE h = ::GetProcessHeap();
        m_internal = h;
    }

    root_allocator::~root_allocator()
    {
        m_internal = nullptr;
    }

    void* root_allocator::internal_reallocate(void* const old, size_t const size, cc::align_val_t const align) noexcept
    {
        return _aligned_realloc(old, size, static_cast<size_t>(align));
    }

    size_t root_allocator::internal_size(void const* ptr) const noexcept
    {
        return _aligned_msize(const_cast<void*>(ptr), 1, 0);
    }
} // namespace cc
