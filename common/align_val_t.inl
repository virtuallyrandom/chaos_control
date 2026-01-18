#pragma once

#include <common/align_val_t.h>

namespace cc
{
    constexpr align_val_t::align_val_t(std::align_val_t v)
        : m_value(static_cast<size_t>(v))
    {
    }

    constexpr align_val_t::align_val_t(size_t v)
        : m_value(v)
    {
    }

    constexpr align_val_t::operator std::align_val_t() const
    {
        return static_cast<std::align_val_t>(m_value);
    }

    constexpr bool align_val_t::operator==(align_val_t const& o) const
    {
        return m_value == o.m_value;
    }

    constexpr bool align_val_t::operator!=(align_val_t const& o) const
    {
        return m_value != o.m_value;
    }

    template <typename T>
    constexpr T align_val_t::as() const
    {
        return static_cast<T>(m_value);
    }

    constexpr align_val_t align_val_t::operator++()
    {
        return align_val_t{ m_value++ };
    }

    constexpr align_val_t& align_val_t::operator++(int)
    {
        m_value++;
        return *this;
    }
} // namespace cc

constexpr inline cc::align_val_t operator~(cc::align_val_t const& av)
{
    return ~av.as<size_t>();
}

constexpr inline cc::align_val_t operator-(cc::align_val_t const& av, size_t sv)
{
    return av.as<size_t>() - sv;
}

constexpr inline cc::align_val_t operator+(cc::align_val_t const& av, size_t sv)
{
    return av.as<size_t>() + sv;
}

constexpr inline cc::align_val_t operator&(cc::align_val_t const& av, size_t sv)
{
    return av.as<size_t>() & sv;
}

constexpr inline cc::align_val_t operator|(cc::align_val_t const& av, size_t sv)
{
    return av.as<size_t>() | sv;
}

constexpr inline cc::align_val_t operator^(cc::align_val_t const& av, size_t sv)
{
    return av.as<size_t>() ^ sv;
}

constexpr inline cc::align_val_t operator&(cc::align_val_t const& av, cc::align_val_t const& sv)
{
    return av.as<size_t>() & sv.as<size_t>();
}

constexpr inline cc::align_val_t operator|(cc::align_val_t const& av, cc::align_val_t const& sv)
{
    return av.as<size_t>() | sv.as<size_t>();
}

constexpr inline cc::align_val_t operator^(cc::align_val_t const& av, cc::align_val_t const& sv)
{
    return av.as<size_t>() ^ sv.as<size_t>();
}

constexpr inline size_t operator-(size_t sv, cc::align_val_t const& av)
{
    return sv - av.as<size_t>();
}

constexpr inline size_t operator+(size_t sv, cc::align_val_t const& av)
{
    return sv + av.as<size_t>();
}

constexpr inline size_t operator&(size_t sv, cc::align_val_t const& av)
{
    return sv & av.as<size_t>();
}

constexpr inline size_t operator|(size_t sv, cc::align_val_t const& av)
{
    return sv | av.as<size_t>();
}

constexpr inline size_t operator^(size_t sv, cc::align_val_t const& av)
{
    return sv ^ av.as<size_t>();
}
