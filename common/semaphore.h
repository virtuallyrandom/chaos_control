#pragma once

#include <common/compiler.h>
#include <common/types.h>
#include <semaphore>

namespace cc
{
    // the std::counting_semaphore has a very weird implementation.
    // it's initial and max values are funky and it's based on a
    // _signed_ counter when that doesn't make functional sense.
    class semaphore : public std::counting_semaphore<PTRDIFF_MAX>
    {
        using std_sema = std::counting_semaphore<PTRDIFF_MAX>;

    public:
        semaphore()
            : std_sema(0)
        {
        }

        semaphore(size_t const initial)
            : std_sema(truncate_cast<ptrdiff_t>(initial))
        {
        }

        void release(size_t const count) noexcept
        {
            std_sema::release(truncate_cast<ptrdiff_t>(count));
        }

    private:
        compiler_disable_copymove(semaphore);
    };
} // namespace cc
