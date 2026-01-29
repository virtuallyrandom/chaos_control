#include <common/allocator_growable.h>
#include <common/debug.h>
#include <utility/lua.h>

#include <external/lua-5.5.0/lua.h>

#pragma comment(lib, "lua-5.5.0.lib")

/*
    lua_State* L = luaL_newstate();

    luaL_openlibs(L);   // Opens all standard Lua libraries into the given state.
    luaL_dostring(L, "print('Hello Lua!')");    // executes a string of Lua code

    lua_close(L);
*/

namespace
{
} // namespace [anonymous]

namespace cc
{
    lua::lua()
    {
/*
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud, unsigned seed);
LUA_API void       (lua_close) (lua_State *L);
LUA_API lua_State *(lua_newthread) (lua_State *L);
LUA_API int        (lua_closethread) (lua_State *L, lua_State *from);

LUA_API lua_CFunction (lua_atpanic) (lua_State *L, lua_CFunction panicf);
*/

        m_state = lua_newstate(realloc, this, 0);

        luaL_openlibs(m_state);
    }

    lua::~lua()
    {
        lua_close(m_state);
    }

    bool lua::execute(char const* const filename) noexcept
    {
        return LUA_OK == luaL_dofile(m_state, filename);
    }

    char const* lua::error() noexcept
    {
        return lua_tostring(m_state, -1);
    }

    void* lua::realloc(void* ud, void* ptr, size_t osize, size_t nsize) noexcept
    {
        lua* const me = reinterpret_cast<lua*>(ud);

        align_val_t align{};
        if (nullptr == ptr)
        {
            switch (osize)
            {
                case LUA_TSTRING:
                case LUA_TTABLE:
                case LUA_TFUNCTION:
                case LUA_TUSERDATA:
                case LUA_TTHREAD:
                    break;

                default:
                    break;
            }
        }

        return me->m_alloc.reallocate(ptr, nsize, align);
    }

} // namespace cc
