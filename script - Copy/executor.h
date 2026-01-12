#pragma once

#include <common/compiler.h>
#include <common/types.h>

namespace cc::script
{
    // load and execute a stream of bytecode
    class executor
    {
    public:
        executor() = default;
        ~executor() = default;

    private:
        compiler_disable_copymove(executor);
    };
} // namespace cc::script
