#include <utility/scheduler.h>

#include <utility/processor_info.h>

namespace cc
{
    scheduler::scheduler(size_t const workerThreadCount)
    {
        if (workerThreadCount != SIZE_MAX)
            m_threadCount = workerThreadCount;
        else
        {
            cc::utility::processor_info pi;
            m_threadCount = pi.getLogicalCoreCount() - 1;
        }

        // c++ interface fail; can't create an array of things and pass them all ctor values
        m_threads = reinterpret_cast<thread*>(new byte[m_threadCount * sizeof(thread)]);
        for( size_t i = 0 ; i < m_threadCount; i++)
            new (m_threads + i) thread(threadFn, this);
    }

    scheduler::~scheduler()
    {
        m_quit = true;
        m_semaphore.release(m_threadCount);

        for (size_t i = 0; i < m_threadCount; i++)
        {
            thread& t = m_threads[i];
            if (t.joinable())
                t.join();
            t.~thread();
        }

        delete[] reinterpret_cast<byte*>(m_threads);

        for (size_t i = 0; i < countof(m_queue); i++)
            assert(m_queue[i].empty());
    }

    void scheduler::dispatch(genericTask_fn const taskFn,
                             void* const taskParam,
                             genericFini_fn const finiFn,
                             void* const finiParam,
                             priority_type const priority,
                             size_t const numThreads)
    {
        size_t const queueIndex = static_cast<size_t>(priority);

        assert(queueIndex < countof(m_queue));
        task_queue& queue = m_queue[queueIndex];
        // this is kind of a hack until I get cooperative scheduling working.
        // the task should be pushed singularly and, when it's descheduled, it then pushes
        // more tasks to a coop queue.
        for (size_t i = 0; i < numThreads; i++)
        {
            if (!queue.push(taskFn, taskParam, finiFn, finiParam, numThreads))
            {
                // TODO: warn or something
                while (!queue.push(taskFn, taskParam, finiFn, finiParam, numThreads));
            }
        }
        m_semaphore.release(numThreads);// 1);
    }

    void scheduler::threadFn(scheduler* const me)
    {
        for (;;)
        {
            me->m_semaphore.acquire();

            if (me->m_quit.load())
                break;

            for (;;)
            {
                bool anyTaskRun = false;

                for (size_t i = 0; i < countof(me->m_queue); i++)
                {
                    task_queue& queue = me->m_queue[i];
                    task_info ti;
                    if (queue.pop(&ti))
                    {
                        if (ti.taskFn != nullptr)
                            ti.taskFn(ti.taskParam);
                        if (ti.finiFn != nullptr)
                            ti.finiFn(ti.finiParam);

                        // start over from highest priority
                        anyTaskRun = true;
                        break;
                    }
                }

                if (!anyTaskRun)
                    break;
            }
        }
    }
} // namespace cc
