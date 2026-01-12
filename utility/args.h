#pragma once

#include <common/string.h>
#include <containers/unordered_map.h>
#include <containers/vector.h>

namespace cc
{
    class args
    {
    public:
        args(size_t argc, char const* const* argv);
        ~args() = default;

        char const* operator[](char const* name) const { return get(name); }

        bool has(char const* name) const;

        char const* get(char const* name) const;
        char const* get(char const* name, char const* defaultValue) const;
        char const* get(char const* name, string const& defaultValue) const;

    private:
        unordered_map<string, string> m_args;
    };
} // namespace cc
