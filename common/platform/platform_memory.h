#pragma once

#include <common/allocator.h>

namespace cc
{
    class root_allocator final : public allocator
    {
    public:
        root_allocator();
        virtual ~root_allocator();

    protected:
        compiler_disable_copymove(root_allocator);

        virtual void* internal_reallocate(void*, size_t, align_val_t) noexcept override;
        virtual size_t internal_size(void const*) const noexcept override;

        void* m_internal;
    };
} // namespace cc
