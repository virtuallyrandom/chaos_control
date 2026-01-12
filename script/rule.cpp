#include <script/rule.h>

#include <common/assert.h>
#include <common/utility.h>

namespace cc::script
{
    static char* dupeStr(char const* const str)
    {
        size_t const len = strlen(str);
        char* const s = new char[len + 1];
        strcpy_s(s, len + 1, str);
        return s;
    }

    static char* dupeStr(string const& str, size_t const start, size_t const end)
    {
        size_t const length = end - start;
        char* const s = new char[length + 1];
        memcpy(s, str.c_str() + start, length);
        s[length] = 0;
        return s;
    }

    constraint::value::value(char const ch)
        : asChar(ch)
    {
    }

    constraint::value::value(char const* const str)
        : asString(dupeStr(str))
    {
    }
 
    constraint::~constraint()
    {
        if (m_type == constraint_type::kAsString || m_type == constraint_type::kAsConstraint)
        {
            #pragma warning(push)
            #pragma warning(disable:5025)
            delete[] m_value.asString;
            #pragma warning(pop)
        }
    }

    constraint::constraint(constraint const& t)
        : m_type(t.m_type)
    {
        if (t.m_type == constraint_type::kAsString || t.m_type == constraint_type::kAsConstraint)
            m_value.asString = dupeStr(t.m_value.asString);
        else
            memcpy(&m_value, &t.m_value, sizeof(m_value));
    }

    constraint::constraint(constraint&& t) noexcept
        : m_type(exchange(t.m_type, constraint_type::kAsChar))
    {
        m_value.asString = exchange(t.m_value.asString, nullptr);
    }

    constraint& constraint::operator=(constraint&& t) noexcept
    {
        m_type = exchange(t.m_type, m_type);
        m_value.asString = exchange(t.m_value.asString, m_value.asString);
        return *this;
    }

    constraint& constraint::operator=(constraint const& t)
    {
        if (this != &t)
        {
            m_type = t.m_type;
            if (t.m_type == constraint_type::kAsString || t.m_type == constraint_type::kAsConstraint)
                m_value.asString = dupeStr(t.m_value.asString);
            else
                memcpy(&m_value, &t.m_value, sizeof(m_value));
        }
        return *this;
    }

    constraint::constraint(char const ch)
        : m_type(constraint_type::kAsChar)
        , m_value(ch)
    {
    }

    constraint::constraint(char const* const str, constraint_type type)
        : m_type(type)
        , m_value(str)
    {
        assert(type == constraint_type::kAsString || type == constraint_type::kAsConstraint);
    }

    constraint::constraint(string const& str, size_t const start, size_t const end, constraint_type const type)
        : m_type(type)
    {
        m_value.asString = dupeStr(str, start, end);
    }

    char constraint::asChar() const
    {
        assert(m_type == constraint_type::kAsChar);
        return m_value.asChar;
    }

    char const* constraint::asString() const
    {
        assert(m_type == constraint_type::kAsString || m_type == constraint_type::kAsConstraint);
        return m_value.asString;
    }

    bool validate(rules const& rules)
    {
        for (cc::pair<string, rule> const& r : rules)
        {
            // verify that all constraints have at least one required character
            bool hasReq = false;
            for (rule::block const& b : r.second.blocks)
            {
                if (b.rate == rule::rate_of::kOnce || b.rate == rule::rate_of::kOneOrMore)
                {
                    hasReq = true;
                    break;
                }
            }

            if (!hasReq)
            {
                assert(hasReq); // rule doesn't include at least one required element so can be satisfied with an empty string
                return false;
            }
        }

        return true;
    }
}