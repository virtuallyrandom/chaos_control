#pragma once

#include <common/allocator.h>
#include <common/compiler.h>
#include <common/memory.h>

namespace cc
{
    class allocator_linear : public allocator
    {
    public:
        virtual ~allocator_linear() = default;

        void reset() noexcept { m_used = {}; }

    protected:
        compiler_disable_copymove(allocator_linear);

        // fixed size, fixed memory, assigned to a parent
        allocator_linear(void*, size_t, allocator&);

        // fixed size, dynamic memory from assigned parent
        allocator_linear(size_t, align_val_t, allocator&);

        // fixed size, dynamic memory from current allocator_top()
        allocator_linear(size_t, align_val_t);

        virtual void* internal_reallocate(void*, size_t, cc::align_val_t) noexcept override;
        virtual size_t internal_size(void const*) const noexcept;
        virtual size_t internal_used() const noexcept;

        size_t m_used{};
    };

    template<size_t Size>
    class allocator_linear_static : public allocator_linear
    {
    public:
        allocator_linear_static(allocator& = allocator_root());
        ~allocator_linear_static() = default;

    private:
        compiler_disable_copymove(allocator_linear_static);
        uint8_t m_buffer[Size];
    };

    class allocator_linear_fixed : public allocator_linear
    {
    public:
        allocator_linear_fixed(void*, size_t, allocator& = allocator_root());
        ~allocator_linear_fixed() = default;

    private:
        compiler_disable_copymove(allocator_linear_fixed);
    };

    class allocator_linear_dynamic : public allocator_linear
    {
    public:
        allocator_linear_dynamic(size_t, align_val_t = {});
        allocator_linear_dynamic(size_t, align_val_t, allocator&);
        ~allocator_linear_dynamic() = default;

    private:
        compiler_disable_copymove(allocator_linear_dynamic);
    };
} // namespace cc

#include <common/allocator_linear.inl>
