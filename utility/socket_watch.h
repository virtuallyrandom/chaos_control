#pragma once

#include <common/atomic.h>
#include <common/compiler.h>
#include <common/mutex.h>
#include <common/types.h>
#include <common/socket.h>
#include <common/thread.h>
#include <containers/static_freelist.h>
#include <containers/vector.h>
#include <utility/scheduler.h>

namespace cc
{
    class scheduler;
} // namespace cc

namespace cc
{
    typedef void (*on_wake_callback)(socket_type const, void* const param);

    class socket_watch
    {
    public:
        socket_watch(scheduler&);
        ~socket_watch();

        void add(socket_type const, on_wake_callback const, void* const param);
        void remove(socket_type const);

        template <typename TypePtr>
        void add(socket_type const sck, void (* const cb)(socket_type, TypePtr), TypePtr param)
        {
            on_wake_callback wcb;
            void* wprm;
            memcpy(&wcb, &cb, sizeof(wcb));
            memcpy(&wprm, &param, sizeof(wprm));
            add(sck, wcb, wprm);
        }

    private:
        static constexpr size_t kMaxConnections = 128;

        struct wake_info
        {
            socket_type const socket;
            cc::on_wake_callback const onWake;
            void* const param;
            socket_watch* const me;

            wake_info(socket_type const s, cc::on_wake_callback const cb, void* const p, socket_watch* const sw)
                : socket(s)
                , onWake(cb)
                , param(p)
                , me(sw)
            {
            }

            compiler_disable_copymove(wake_info);
        };

        static void addToList(cc::atomic<wake_info*>(&)[kMaxConnections], wake_info*);
        static bool removeFromList(cc::atomic<wake_info*>(&)[kMaxConnections], wake_info*);
        static wake_info* removeFromList(cc::atomic<wake_info*>(&)[kMaxConnections], socket_type);

        static void onWakeExec(wake_info*);
        static void onWakeFinished(wake_info*);
        static void wake(socket_watch*);
        static void threadProc(socket_watch*);

        cc::scheduler& m_scheduler;
        cc::thread m_workerThread;

        cc::atomic<bool> m_quit{ false };
        cc::byte m_pad0[7]{};

        socket_type m_controlSend = kInvalidSocket;
        socket_type m_controlRecv = kInvalidSocket;

        cc::atomic<wake_info*> m_selectInfo[kMaxConnections]{ nullptr };
        cc::atomic<wake_info*> m_flightInfo[kMaxConnections]{ nullptr };

        cc::byte m_pad1[16];
        cc::static_freelist<wake_info, kMaxConnections> m_infoPool;

        compiler_disable_copymove(socket_watch);
    };
} // namespace cc
