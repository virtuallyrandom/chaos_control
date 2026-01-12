#include "cc_connect.h"

#include <common/algorithm.h>
#include <utility/console.h>
#include <utility/scheduler.h>
#include <utility/socket_watch.h>


void cc_connect::transaction::set_status(cc_connect::status s)
{
    status = s;
    if (nullptr != callback)
        callback(callback_param, status);
}

cc_connect::transaction_open::transaction_open(socket_type& sck, char const* const at_addr, uint16_t const on_port)
    : socket(sck)
    , addr(at_addr)
    , port(on_port)
{
}

bool cc_connect::transaction_open::execute()
{
    // ideally this would be broken up into discrete tasks and use the
    // socket watch to get notifications that a connection has occurred,
    // then do notifications.
    socket = cc::socket::TCPConnect(addr, port);
    set_status((kInvalidSocket == socket) ? cc_connect::kFail : cc_connect::kOpened);
    return true;
}

cc_connect::transaction_close::transaction_close(socket_type& sck)
    : socket(sck)
{
}

bool cc_connect::transaction_close::execute()
{
    // signal the socket to shut down, which will cause the socket watcher to
    // wake the socket, thus closing it and removing it from the transaction list.
    cc::socket::shutdown(socket, cc::socket::kBoth);
    return true;
}

cc_connect::cc_connect(cc::console& con, cc::scheduler& sch, cc::socket_watch& sw)
    : m_console(con)
    , m_scheduler(sch)
    , m_socket_watch(sw)
{
}

cc_connect::~cc_connect()
{
    close();

    // todo: make this better
    while (kInvalidSocket != m_socket)
        ;
}

void cc_connect::open(char const* addr, uint16_t port, status_callback cb, void* param)
{
    launch<transaction_open>(cb, param, kOpening, m_socket, addr, port);
}

void cc_connect::close()
{
    // todo: wait for pending transations to complete

    if (kInvalidSocket != m_socket)
    {
        cc::socket::close(m_socket);
        m_socket = kInvalidSocket;
    }
}

void cc_connect::register_for(group_id, ident_id, receive_callback, void* param)
{
}

void cc_connect::unregister_for(group_id, ident_id, receive_callback)
{
}

void cc_connect::send(packet const*)
{
}

void cc_connect::do_execute(transaction* const me)
{
    bool const finished = me->execute();

    me->set_status(me->status);

    // not done yet, requeue it
    if (!finished)
        me->connect->m_scheduler.dispatch(do_execute, me);

    // it's finished, remove the transaction
    else
    {
        cc::unique_lock lock(me->connect->m_transaction_lock);
        list_of_transactions::iterator const iter = cc::find(me->connect->m_transactions.begin(), me->connect->m_transactions.end(), me);
        if (iter != me->connect->m_transactions.end())
            me->connect->m_transactions.erase(iter);
    }
}

void cc_connect::on_socket(socket_type const sck, cc_connect* const me)
{
    // fixme: store this to a ring buffer and distribute from there

    const cc::system_clock::time_point tp{ cc::system_clock::now() };

    uint32_t count{};
    int rv = cc::socket::ioctl(sck, cc::socket::kFioNRead, &count);
    if (0 != rv || 0 == count)
    {
        me->m_socket_watch.remove(sck);
        cc::socket::close(me->m_socket);
        me->m_socket = kInvalidSocket;
        return;
    }

    uint8_t* const buffer = reinterpret_cast<uint8_t*>(_alloca(count));

    int const len = cc::socket::recv(sck, buffer, count, 0);
    if (len <= 0)
    {
        me->m_socket_watch.remove(sck);
        cc::socket::close(me->m_socket);
        me->m_socket = kInvalidSocket;
        return;
    }

    printf("Received %u bytes: \n", count);
    for (size_t i = 0; i < count; i++)
        printf(" %02x", buffer[i]);

    // distribute buffer/count 
}
