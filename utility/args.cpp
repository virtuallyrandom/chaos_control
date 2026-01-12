#include <utility/args.h>

namespace cc
{
    args::args(size_t const argc, char const* const* const argv)
    {
        string* value{ nullptr };
        for (size_t i = 1; i < argc; i++)
        {
            if (argv[i][0] == '-')
                value = &m_args[argv[i] + 1];
            else if (value != nullptr)
                *value = argv[i];
        }
    }

    bool args::has(char const* const name) const
    {
        return m_args.end() != m_args.find(name);
    }

    char const* args::get(char const* name) const
    {
        return get(name, nullptr);
    }

    char const* args::get(char const* name, char const* defaultValue) const
    {
        unordered_map<string, string>::const_iterator iter = m_args.find(name);
        if (m_args.end() == iter)
            return defaultValue;
        return iter->second.c_str();
    }

    char const* args::get(char const* name, string const& defaultValue) const
    {
        unordered_map<string, string>::const_iterator iter = m_args.find(name);
        if (m_args.end() == iter)
            return defaultValue.c_str();
        return iter->second.c_str();
    }
} // namespace cc
