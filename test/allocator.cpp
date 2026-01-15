#include "test.h"

#include <common/allocator.h>

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

    virtual void* internal_reallocate(void* const ptr, size_t const size, cc::align_val_t const alignv) noexcept override
    {
        if (size == 0 && nullptr != ptr)
            return nullptr;

        size_t const align = static_cast<size_t>(alignv);
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

    virtual cc::string operator()() override
    {
        cc::string error;

        try
        {
            linear_allocator la;

            void* p[4];
            p[0] = la.allocate(1);
            la.deallocate(p[0]);

            cc::push_allocator(la);

            int* i = new int;

            cc::pop_allocator();
            delete i;
        }
        catch (...)
        {
        }
        return error;
    }

    virtual const char* name() const override
    {
        return "allocator";
    }
} allocator_test;
