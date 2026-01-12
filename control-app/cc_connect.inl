#pragma once

#include "cc_connect.h"

#include <common/type_traits.h>
#include <utility/scheduler.h>

template <typename Type>
void cc_connect::open(char const* addr, uint16_t port, void (*cb)(Type*, status), Type* param)
{
    status_callback scb;
    static_assert(sizeof(scb) == sizeof(cb));
    memcpy(&scb, &cb, sizeof(scb));

    void* scp;
    static_assert(sizeof(scp) == sizeof(Type*));
    memcpy(&scp, &param, sizeof(Type*));

    open(addr, port, scb, scp);
}

template <typename Type>
void cc_connect::register_for(group_id group, ident_id ident, void (*cb)(Type*, packet const*), Type* param)
{
    receive_callback rcb;
    static_assert(sizeof(rcb) == sizeof(cb));
    memcpy(&rcb, &cb, sizeof(rcb));

    void* rcp;
    static_assert(sizeof(rcp) == sizeof(Type*));
    memcpy(&rcp, &param, sizeof(Type*));

    register_for(group, ident, rcb, rcp);
}

template <typename Type>
void cc_connect::unregister_for(group_id group, ident_id ident, void (*cb)(Type*, packet const*), Type* param)
{
    receive_callback rcb;
    static_assert(sizeof(rcb) == sizeof(cb));
    memcpy(&rcb, &cb, sizeof(rcb));

    void* rcp;
    static_assert(sizeof(rcp) == sizeof(Type*));
    memcpy(&rcp, &param, sizeof(Type*));

    unregister_for(group, ident, rcb, rcp);
}

template <typename Type, class... Args>
void cc_connect::launch(status_callback const cb, void* const param, cc_connect::status initial_status, Args&&... args)
{
    transaction* const t = new Type(cc::forward<Args>(args)...);

    t->connect = this;
    t->callback = cb;
    t->callback_param = param;

    {
        cc::unique_lock lock(m_transaction_lock);
        m_transactions.push_back(t);
    }

    t->set_status(initial_status);
    m_scheduler.dispatch(do_execute, t);
}
