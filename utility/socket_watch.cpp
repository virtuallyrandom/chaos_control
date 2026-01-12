#include <utility/socket_watch.h>

#include <common/assert.h>
#include <common/concurrency.h>
#include <common/platform/winsock.h>
#include <common/thread.h>

namespace cc
{
    socket_watch::socket_watch(scheduler& sch)
        : m_scheduler(sch)
    {
        socket_type pair[2];
        int const err = cc::socket::socketpair(cc::socket::kInetV4, cc::socket::kStream, cc::socket::kTcp , pair);
        assert(err == 0);
        m_controlRecv = pair[0];
        m_controlSend = pair[1];

        m_workerThread = cc::thread(threadProc, this);
    }

    socket_watch::~socket_watch()
    {
        if (m_workerThread.joinable())
        {
            m_quit.store(true);
            wake(this);
            m_workerThread.join();
        }

        cc::socket::close(m_controlRecv);
        cc::socket::close(m_controlSend);
    }

    void socket_watch::add(socket_type const sck, on_wake_callback const wakeCB, void* const param)
    {
        wake_info* const info = m_infoPool.acquire(sck, wakeCB, param, this);
        printf("add sck[%zu] to info[%p]\n", sck, info);
        addToList(m_selectInfo, info);
        wake(this);
    }

    void socket_watch::remove(socket_type const sck)
    {
        wake_info* info = removeFromList(m_flightInfo, sck);
        if (info == nullptr)
        {
            info = removeFromList(m_selectInfo, sck);
            wake(this);
        }
        assert(info != nullptr);
        m_infoPool.release(info);
    }

    void socket_watch::addToList(cc::atomic<wake_info*>(&list)[kMaxConnections], wake_info* const info)
    {
        for (size_t i = 0; i < kMaxConnections; i++)
        {
            wake_info* was = nullptr;
            if (list[i].compare_exchange_weak(was, info))
                break;
        }
    }

    bool socket_watch::removeFromList(cc::atomic<wake_info*>(&list)[kMaxConnections], wake_info* const info)
    {
        for (size_t i = 0; i < kMaxConnections; i++)
        {
            wake_info* was = info;
            if (list[i].compare_exchange_weak(was, nullptr))
                return true;
        }
        return false;
    }

    socket_watch::wake_info* socket_watch::removeFromList(cc::atomic<wake_info*>(&list)[kMaxConnections], socket_type const sck)
    {
        for (size_t i = 0; i < kMaxConnections; i++)
        {
            wake_info* was = list[i].load();
            if (was == nullptr)
                continue;
            if (was->socket != sck)
                continue;

            // couldn't swap it out (it was already swapped out) so restart the search
            if (!list[i].compare_exchange_weak(was, nullptr))
            {
                i = SIZE_MAX;
                continue;
            }

            return was;
        }

        return nullptr;
    }

    void socket_watch::onWakeExec(wake_info* const info)
    {
        if (info->onWake != nullptr)
            info->onWake(info->socket, info->param);
    }

    void socket_watch::onWakeFinished(wake_info* const info)
    {
        // task is finished; add it back to the wait list
        if (removeFromList(info->me->m_flightInfo, info))
            addToList(info->me->m_selectInfo, info);

        wake(info->me);
    }

    void socket_watch::wake(socket_watch* const me)
    {
        char b = 0;
        (void)send(me->m_controlSend, &b, 1, 0);
    }

    void socket_watch::threadProc(socket_watch* const me)
    {
        for (;;)
        {
            fd_set set;
            FD_ZERO(&set);

            FD_SET(me->m_controlRecv, &set);
            socket_type highSocket = me->m_controlRecv;

            for (size_t i = 0; i < countof(me->m_selectInfo); i++)
            {
                wake_info* const info = me->m_selectInfo[i].load();

                if (info == nullptr)
                    continue;

                assert(info->socket != kInvalidSocket);
                FD_SET(info->socket, &set);
                highSocket = cc::max(highSocket, info->socket);
            }

            int ready = select((int)(highSocket + 1), &set, nullptr, nullptr, nullptr);
            if (ready == 0)
                continue;

            if (ready == -1)
                printf("ERROR: %u\n", cc::socket::get_error());

            if (me->m_quit)
                break;

            // did this contain a control wake up?
            if (FD_ISSET(me->m_controlRecv, &set))
            {
                char buffer = 0;
                (void)recv(me->m_controlRecv, &buffer, sizeof(buffer), 0);
                ready--;
                if (ready == 0)
                    continue;
            }

            // also (or only) contained other socket events, so find the sockets with events,
            // remove them from the wait list, and dispatch a job to perform the read/write.
            for (size_t i = 0; ready != 0 && i < countof(me->m_selectInfo); i++)
            {
                wake_info* const info = me->m_selectInfo[i].load();
                if (info == nullptr)
                    continue;

                if (!FD_ISSET(info->socket, &set))
                    continue;

                me->m_selectInfo[i].store(nullptr);
                addToList(me->m_flightInfo, info);

                me->m_scheduler.dispatch(onWakeExec, info, onWakeFinished, info);

                ready--;
            }
        }
    }
} // namespace cc
