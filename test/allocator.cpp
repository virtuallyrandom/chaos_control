#include "test.h"

#include <common/allocator.h>
#include <common/debug.h>
#include <common/format.h>
#include <common/allocator_linear.h>
#include <common/math.h>

#define check(test)\
    if (!(test))\
    {\
        if (cc::is_debugging())\
            __debugbreak();\
        return cc::format("Failed: {}\n", #test);\
    }

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

    cc::string reset(cc::allocator_linear& alloc)
    {
        alloc.reset();
    }

    cc::string exhaust(cc::allocator_linear& alloc, size_t const size, size_t align)
    {
        size_t alloc_counter = alloc.num_allocs();
        size_t free_counter = alloc.num_frees();

        for(;;)
        {
            void* const p = alloc.allocate(size, align);

            if (nullptr == p)
                break;

            alloc_counter++;
            check(nullptr != p);
            check(alloc_counter == alloc.num_allocs());

            alloc.deallocate(p);
            free_counter++;
            check(free_counter == alloc.num_frees());
        }

        check(alloc.used() <= alloc.capacity());
        check(alloc.available() < cc::max(size, align));

        return "";
    }

    cc::string test_linear_allocator(cc::allocator_linear& alloc)
    {
        cc::string errmsg;

        alloc.reset();

        check(0 == alloc.num_allocs());
        check(0 == alloc.num_frees());

        for (size_t n = 1; n <= 256; n *= 2)
        {
            errmsg = exhaust(alloc, n, n);
            if (!errmsg.empty())
                return errmsg;
        }

#if 0
        cc::allocator_push(la);

        int* i = new int;

        check_alloc_count(la, 2, 1, error);

        cc::allocator_pop();
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
#endif // 0

        return "";
    }

    virtual cc::string operator()() override
    {
        cc::string error;

        try
        {
            cc::allocator_linear_static<256> la;

            error += test_linear_allocator(la);

#if 0
            check_alloc_count(la, 0, 0, error);

            void* p;
            p = la.allocate(1);
            check_alloc_count(la, 1, 0, error);

            la.deallocate(p);
            check_alloc_count(la, 1, 1, error);

            cc::allocator_push(la);

            int* i = new int;

            check_alloc_count(la, 2, 1, error);

            cc::allocator_pop();
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
#endif // 0
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
