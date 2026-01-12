#pragma once

#include <common/atomic.h>
#include <common/compiler.h>
#include <common/semaphore.h>
#include <common/thread.h>
#include <common/types.h>
#include <containers/static_queue.h>

namespace cc
{
    using genericTask_fn = void(*)(void* const);
    using genericFini_fn = void(*)(void* const);

    enum class priority_type : uint8_t
    {
        kHigh,
        kNormal,
        kLow,

        Count
    };

    class scheduler
    {
    public:
        // if left as SIZE_MAX, limits the number of worker threads to the logical
        // core count, minus one.
        scheduler(size_t workerThreadCount = SIZE_MAX);

        ~scheduler();

        void dispatch(genericTask_fn const,
                      void* const taskParam = nullptr,
                      genericFini_fn const = nullptr,
                      void* const finiParam = nullptr,
                      priority_type const priority = priority_type::kNormal,
                      size_t const numThreads = 1);

        template< typename TaskArgPtr = nullptr_t, typename FiniArgPtr = nullptr_t>
        void dispatch(void(*tfn)(TaskArgPtr),
                      TaskArgPtr const tap = nullptr,
                      void(*ffn)(FiniArgPtr) = nullptr,
                      FiniArgPtr const fap = nullptr,
                      priority_type const priority = priority_type::kNormal,
                      size_t const numThreads = 1)
        {
            genericTask_fn gtfn;
            memcpy(&gtfn, &tfn, sizeof(gtfn));
            genericFini_fn gffn;
            memcpy(&gffn, &ffn, sizeof(gffn));
            dispatch(gtfn, tap, gffn, fap, priority, numThreads);
        }

    private:
        static constexpr size_t kQueueCount = 256;

        struct task_info
        {
            cc::genericTask_fn taskFn;
            void* taskParam;
            cc::genericFini_fn finiFn;
            void* finiParam;
            size_t numThreads;
        };

        using task_queue = cc::static_queue<task_info, kQueueCount>;

        static void threadFn(scheduler*);

        cc::atomic<bool> m_quit = false;
        uint8_t pad[7]{};
        cc::semaphore m_semaphore;
        cc::thread* m_threads = nullptr;
        size_t m_threadCount = 0;
        task_queue m_queue[static_cast<size_t>(cc::priority_type::Count)];

        compiler_disable_copymove(scheduler);

    };
} // namespace cc::scheduler
