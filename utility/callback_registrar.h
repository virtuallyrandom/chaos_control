#pragma once

#include <containers/vector.h>

namespace cc
{
    template<typename Callback, typename ParamPtr>
    class callback_registrar
    {
    public:
        callback_registrar() = default;

        void add(Callback, ParamPtr);
        void remove(Callback, ParamPtr);

        template<class ...Parameters>
        void invoke(Parameters&&...);

    private:
        struct CBInfo
        {
            Callback cb;
            ParamPtr param;
        };

        cc::vector<CBInfo> m_callbacks;
    };
} // namespace cc

#include <utility/callback_registrar.inl>