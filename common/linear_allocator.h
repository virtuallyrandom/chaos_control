#pragma once

#include <common/allocator.h>
#include <common/compiler.h>
#include <common/memory.h>

namespace cc
{
    class linear_allocator : public allocator
    {
    public:
        virtual ~linear_allocator() = default;

        void reset() noexcept { m_used = {}; }

    protected:
        compiler_disable_copymove(linear_allocator);

        // fixed size, fixed memory, assigned to a parent
        linear_allocator(void*, size_t, allocator&);

        // fixed size, dynamic memory from assigned parent
        linear_allocator(size_t, align_val_t, allocator&);

        // fixed size, dynamic memory from current allocator_top()
        linear_allocator(size_t, align_val_t);

        virtual void* internal_reallocate(void*, size_t, cc::align_val_t) noexcept override;
        virtual size_t internal_size(void const*) const noexcept;
        virtual size_t internal_used() const noexcept;

        size_t m_used{};
    };

    template<size_t Size>
    class linear_allocator_static : public linear_allocator
    {
    public:
        linear_allocator_static(allocator& = allocator_root());
        ~linear_allocator_static() = default;

    private:
        compiler_disable_copymove(linear_allocator_static);
        uint8_t m_buffer[Size];
    };

    class linear_allocator_fixed : public linear_allocator
    {
    public:
        linear_allocator_fixed(void*, size_t, allocator& = allocator_root());
        ~linear_allocator_fixed() = default;

    private:
        compiler_disable_copymove(linear_allocator_fixed);
    };

    class linear_allocator_dynamic : public linear_allocator
    {
    public:
        linear_allocator_dynamic(size_t, align_val_t = {});
        linear_allocator_dynamic(size_t, align_val_t, allocator&);
        ~linear_allocator_dynamic() = default;

    private:
        compiler_disable_copymove(linear_allocator_dynamic);
    };
} // namespace cc

#include <common/linear_allocator.inl>
