#pragma once

#include <common/compiler.h>

namespace cc
{
    class console;

    class service_manager
    {
    public:
        enum Mode
        {
            kAuto,
            kDemand,
        };

        service_manager(console&);
        ~service_manager();

        bool install(char const* serviceName,
                     Mode,
                     char const* friendlyName,
                     char const* description,
                     char const* exePath);
        bool uninstall(char const* serviceName);

        bool enable(char const* serviceName);
        bool disable(char const* serviceName);

        bool query(char const* serviceName);

        bool start(char const* serviceName);
        bool stop(char const* serviceName);

        bool restart(char const* serviceName);
        bool commit(char const* serviceName);

    private:
        console& m_console;

        service_manager() = delete;
        compiler_disable_copymove(service_manager);
    };
} // namespace cc
