#pragma once

#include <common/mutex.h>
#include <common/socket.h>
#include <common/types.h>
#include <containers/unordered_map.h>
#include <containers/vector.h>

using group_id = uint16_t;
using ident_id = uint16_t;

struct packet
{
    group_id group;
    ident_id ident;
    uint32_t size; // includes size of packet
    uint64_t time; // millisecond resolution
};

namespace cc
{
    class console;
    class scheduler;
    class socket_watch;
} // namespace cc

class cc_connect
{
public:
    enum status
    {
        kSuccess,
        kFail,
        kOpening,
        kOpened,
        kClosing,
        kClosed,
        kNone,
   };

    using status_callback = void(*)(void*, status);
    using receive_callback = void(*)(void*, packet const*);

    cc_connect(cc::console&, cc::scheduler&, cc::socket_watch&);
    ~cc_connect();

    void open(char const* addr, uint16_t port, status_callback, void* param);
    void close();

    void register_for(group_id, ident_id, receive_callback, void* param);
    void unregister_for(group_id, ident_id, receive_callback);

    void send(packet const*);

    // helpers

    template <typename Type>
    void open(char const* addr, uint16_t port, void (*cb)(Type*, status), Type* param);

    template <typename Type>
    void register_for(group_id, ident_id, void (*cb)(Type*, packet const*), Type* param);

    template <typename Type>
    void unregister_for(group_id, ident_id, void (*cb)(Type*, packet const*), Type* param);

private:
    struct transaction
    {
        cc_connect* connect{ nullptr };
        status status{ cc_connect::kNone };
        status_callback callback;
        void* callback_param;

        virtual bool execute() = 0;

        void set_status(cc_connect::status);
    };

    struct receiver
    {
        receive_callback cb;
        void* param;
    };

    struct transaction_open : public transaction
    {
        transaction_open(socket_type&, char const*, uint16_t);

        socket_type& socket;
        char const* const addr;
        uint16_t const port;

        virtual bool execute() override;
    };

    struct transaction_close : public transaction
    {
        transaction_close(socket_type&);

        socket_type& socket;

        virtual bool execute() override;
    };

    using list_of_transactions = cc::vector<transaction*>;
    using list_of_receivers = cc::vector<receiver>;
    using map_of_idents = cc::unordered_map<ident_id, list_of_receivers>;
    using map_of_groups = cc::unordered_map<group_id, map_of_idents>;

    static void do_execute(transaction*);
    static void on_socket(socket_type, cc_connect*);

    template <typename Type, class... Args>
    void launch(status_callback const cb, void* const param, cc_connect::status initial_status, Args&&...);

    cc::console& m_console;
    cc::scheduler& m_scheduler;
    cc::socket_watch& m_socket_watch;
    socket_type m_socket;
    map_of_groups m_receiver; // m_receiver[group_id][ident_id] <- vector of receivers

    cc::shared_timed_mutex m_transaction_lock;
    list_of_transactions m_transactions;
};

#include "cc_connect.inl"
