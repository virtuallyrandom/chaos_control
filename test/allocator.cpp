#include "test.h"

#include <common/allocator.h>
#include <common/format.h>

class linear_allocator : public cc::allocator
{
public:
    linear_allocator()
    {
        add_page(m_buffer, kBufferSize);
    }

    virtual ~linear_allocator() = default;

protected:
    compiler_disable_copymove(linear_allocator);

    virtual void* internal_reallocate(void* const ptr, size_t const size, cc::align_val_t const align) noexcept override
    {
        if (size == 0 && nullptr != ptr)
            return nullptr;

        m_used = (m_used + (align - 1)) & ~(align - 1);

        uint8_t* const mem = m_buffer + m_used;
        m_used += size;

        if (m_used >= kBufferSize)
            return nullptr;

        return mem;
    }

    virtual size_t internal_size(void const*) const noexcept override
    {
        return 0;
    }

private:
    static constexpr size_t kBufferSize = 1024;

    uint8_t m_buffer[kBufferSize];
    size_t m_used;
};

class allocator_test : public cc::test
{
public:
    allocator_test() = default;

    void check_alloc_count(cc::allocator& allocator, size_t num_allocs, size_t num_frees, std::string& error)
    {
        if (allocator.num_allocs() != num_allocs)
            error += cc::format("Invalid allocation count; has {} expected {}.\n", allocator.num_allocs(), num_allocs);

        if (allocator.num_frees() != num_frees)
            error += cc::format("Invalid deallocation count: has {} expected {}.\n", allocator.num_frees(), num_frees);
    }

    virtual cc::string operator()() override
    {
        cc::string error;

        try
        {
            linear_allocator la;

            check_alloc_count(la, 0, 0, error);

            void* p;
            p = la.allocate(1);
            check_alloc_count(la, 1, 0, error);

            la.deallocate(p);
            check_alloc_count(la, 1, 1, error);

            cc::push_allocator(la);

            int* i = new int;

            check_alloc_count(la, 2, 1, error);

            cc::pop_allocator();
            delete i;

            check_alloc_count(la, 2, 2, error);

            // realloc to make a new allocation
            p = la.reallocate(nullptr, 4, alignof(uint32_t));
            check_alloc_count(la, 3, 2, error);

            // realloc to free this one and make a new one (note la doesn't actually free)
            p = la.reallocate(p, 8, alignof(uint32_t));
            check_alloc_count(la, 4, 3, error);

            p = la.reallocate(p, 0);
            check_alloc_count(la, 4, 4, error);
        }
        catch (...)
        {
            error += "Exception occurred";
        }
        return error;
    }

    virtual const char* name() const override
    {
        return "allocator";
    }
} allocator_test;
