#pragma once

#include <common/allocator_growable.h>
#include <common/compiler.h>
#include <common/string.h>

struct lua_State;

namespace cc
{
    class lua
    {
    public:
        lua();
        ~lua();

        bool execute(char const* filename) noexcept;
        char const* error() noexcept;

    private:
        compiler_disable_copymove(lua);

        static void* realloc(void*, void*, size_t, size_t) noexcept;

        allocator_growable m_alloc;
        lua_State* m_state{};
    };
} // namespace cc
