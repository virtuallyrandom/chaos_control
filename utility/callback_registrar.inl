#pragma once

#include <utility/callback_registrar.h>

namespace cc
{
    template<typename Callback, typename ParamPtr>
    void callback_registrar<Callback, ParamPtr>::add(Callback const cb, ParamPtr const prm)
    {
        m_callbacks.push_back({ cb, prm });
    }

    template<typename Callback, typename ParamPtr>
    void callback_registrar<Callback, ParamPtr>::remove(Callback const cb, ParamPtr const prm)
    {
        size_t dst{ 0 };

        const CBInfo info{ cb, prm };

        for (size_t src = 0; src < m_callbacks.length(); src++)
        {
            if (memcmp(&m_callbacks[src], &info, sizeof(info)) != 0)
                m_callbacks[dst++] = m_callbacks[src];
        }

        if (dst != m_callbacks.length())
            m_callbacks.resize(dst);
    }

    template<typename Callback, typename ParamPtr>
    template<class ...Parameters>
    void callback_registrar<Callback, ParamPtr>::invoke(Parameters&&... params)
    {
        for (size_t i = 0; i < m_callbacks.length(); i++)
            m_callbacks[i].cb(m_callbacks[i].param, std::forward<Parameters>(params)...);
    }
} // namespace cc
