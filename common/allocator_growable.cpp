#include <common/allocator_growable.h>
#include <common/math.h>

#include <external/dlmalloc/dlmalloc.h>

#pragma comment(lib, "dlmalloc.lib")

namespace cc
{
    allocator_growable::allocator_growable(size_t const initial_size)
    {
        m_internal = create_mspace(initial_size, 0, on_mmap, this);
    }

    allocator_growable::~allocator_growable()
    {
        destroy_mspace(m_internal);
    }


    void allocator_growable::on_mmap(void* const user, void* const ptr, size_t const size) noexcept
    {
        allocator_growable* const me = reinterpret_cast<allocator_growable*>(user);
        me->add_page(allocator::page::kNone, ptr, size);
    }

    void* allocator_growable::internal_reallocate(void* const old_ptr, size_t const new_size, align_val_t const align) noexcept
    {
        if (nullptr == m_internal)
            return nullptr;

        if (0 == new_size)
        {
            mspace_free(m_internal, old_ptr);
            return nullptr;
        }

        if (nullptr == old_ptr)
            return mspace_memalign(m_internal, align.as<size_t>(), new_size);

        void* new_ptr = mspace_realloc_in_place(m_internal, old_ptr, new_size);
        if (nullptr != new_ptr)
            return new_ptr;

        size_t const old_size = mspace_usable_size(old_ptr);
        size_t const copy_size = min(old_size, new_size);

        new_ptr = mspace_memalign(m_internal, align.as<size_t>(), new_size);
        if (nullptr == new_ptr)
            return nullptr;

        memcpy(new_ptr, old_ptr, copy_size);
        return new_ptr;
    }

    size_t allocator_growable::internal_size(void const* const ptr) const noexcept
    {
        if (nullptr == m_internal)
            return 0;

        if (nullptr == ptr)
            return 0;

        return mspace_usable_size(ptr);
    }

    size_t allocator_growable::internal_used() const noexcept
    {
        if (nullptr == m_internal)
            return 0;

        return mspace_footprint(m_internal);
    }
} // namespace cc
