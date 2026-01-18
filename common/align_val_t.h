#pragma once

#include <common/compiler.h>

namespace cc
{
    class align_val_t
    {
    public:
        compiler_default_ctors(align_val_t);

        constexpr align_val_t(std::align_val_t);

        constexpr align_val_t(size_t);

        constexpr operator std::align_val_t() const;

        constexpr bool operator==(align_val_t const&) const;
        constexpr bool operator!=(align_val_t const&) const;

        template <typename T>
        constexpr T as() const;

        constexpr align_val_t operator++();
        constexpr align_val_t& operator++(int);

    private:
        size_t m_value;
    };

} // namespace cc

constexpr cc::align_val_t operator~(cc::align_val_t const&);

constexpr cc::align_val_t operator-(cc::align_val_t const&, size_t);
constexpr cc::align_val_t operator+(cc::align_val_t const&, size_t);
constexpr cc::align_val_t operator&(cc::align_val_t const&, size_t);
constexpr cc::align_val_t operator|(cc::align_val_t const&, size_t);
constexpr cc::align_val_t operator^(cc::align_val_t const&, size_t);

constexpr cc::align_val_t operator&(cc::align_val_t const&, cc::align_val_t const&);
constexpr cc::align_val_t operator|(cc::align_val_t const&, cc::align_val_t const&);
constexpr cc::align_val_t operator^(cc::align_val_t const&, cc::align_val_t const&);

constexpr size_t operator-(size_t, cc::align_val_t const& );
constexpr size_t operator+(size_t, cc::align_val_t const& );
constexpr size_t operator&(size_t, cc::align_val_t const& );
constexpr size_t operator|(size_t, cc::align_val_t const& );
constexpr size_t operator^(size_t, cc::align_val_t const& );

#include <common/align_val_t.inl>
