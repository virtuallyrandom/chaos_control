#include <containers/static_freelist.h>
#include <containers/static_queue.h>

namespace
{
    static bool TestFreeList()
    {
        constexpr size_t numTests = 4;
        constexpr size_t count = 256;
        cc::static_freelist< int, count > q;

        for (size_t m = 0; m < numTests; m++)
        {
            int* p[count];
            for (size_t i = 0; i < count; i++)
            {
                p[i] = q.acquire();
                assert(p[i] != nullptr);
            }

            int* np = q.acquire();
            assert(np == nullptr);

            for (size_t i = count; i != 0; i--)
                q.release(p[i - 1]);
        }

        return true;
    }

    bool TestQueue()
    {
        constexpr size_t numTests = 4;
        constexpr size_t count = 256;
        cc::static_queue< int, count > q;

        for (size_t m = 0; m < numTests; m++)
        {
            int* p[count];
            for (size_t i = 0; i < count; i++)
            {
                p[i] = q.write_acquire();
                assert(p[i] != nullptr);
            }

            int* np = q.write_acquire();
            assert(np == nullptr);

            for (size_t i = count; i != 0; i--)
                q.write_release(p[i - 1]);

            int* r[count];
            for (size_t i = 0; i < count; i++)
                r[i] = q.read_acquire();
            int* nr = q.read_acquire();
            assert(nr == nullptr);

            for (size_t i = count; i != 0; i--)
                q.read_release(r[i - 1]);
        }

        return true;
    }

    bool Test()
    {
        return TestFreeList() && TestQueue();
    }
} // namespace [anonymous]

namespace cc
{
    bool gContainerTestsPass = Test();
} // namespace cc
