#pragma once

#include <common/allocator.h>

namespace cc
{
    class allocator_growable final : public allocator
    {
    public:
        allocator_growable(size_t initial_size = 0);
        ~allocator_growable();

    private:
        compiler_disable_copymove(allocator_growable);

        static void on_mmap(void*, void*, size_t) noexcept;

        virtual void* internal_reallocate(void*, size_t size, align_val_t) noexcept override;
        virtual size_t internal_size(void const*) const noexcept override;
        virtual size_t internal_used() const noexcept override;

        void* m_internal;
    };
} // namespace cc
