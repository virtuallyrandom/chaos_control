#include <common/allocator.h>
#include <common/memory.h>

namespace cc
{
    inline void* allocator::reallocate(void* const ptr, size_t const size, align_val_t const align) noexcept
    {
        if (nullptr != ptr)
            m_frees++;
        if (0 != size)
            m_allocs++;

        return internal_reallocate(ptr, size, align);
    }

    inline void* allocator::allocate(size_t const size, align_val_t const align_val) noexcept
    {
        static constexpr align_val_t kDefaultAlignment{ 8 };
        static constexpr align_val_t kZeroAlignment{};

        align_val_t const align = align_val == kZeroAlignment ? kDefaultAlignment : align_val;
        assert((align & (align - 1)) == 0); // not a power of two

        void* const result = internal_reallocate(nullptr, size, align);

        if (nullptr != result)
            m_allocs++;

        return result;
    }

    // free memory from the current heap. pointer may be nullptr.
    inline void allocator::deallocate(void* ptr) noexcept
    {
        if (nullptr == ptr)
            return;

        internal_reallocate(ptr, 0, align_val_t{});
        m_frees++;
    }

    inline size_t allocator::size(void const* ptr) const noexcept
    {
        return internal_size(ptr);
    }
} // namespace cc
