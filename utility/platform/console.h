#pragma once

#include <common/socket.h>
#include <common/stdio.h>

namespace cc
{
    namespace console_platform
    {
        void redirect(FILE* const from, socket_type const to, int(&info)[2]);
        void restore(int(&info)[2]);
    } // namespace console::platform
} // namespace cc
