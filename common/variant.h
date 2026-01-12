#pragma once

#include <variant>

#include <common/assert.h>
#include <common/type_traits.h>

namespace cc
{
    template<class... Args>
    class variant : public std::variant<Args...>
    {
    public:
        using std::variant<Args...>::operator=;
        using std::variant<Args...>::index;
        using std::variant<Args...>::valueless_by_exception;
        using std::variant<Args...>::emplace;
        using std::variant<Args...>::swap;

        template<typename T>
        constexpr operator T& ();

        template<typename T>
        constexpr operator const T& () const;

        // does the variant hold the specific type
        template<typename T>
        constexpr bool holds() const;

        // get the type; asserts if it isn't what's held
        template<typename T>
        constexpr T& get();
        template<typename T>
        constexpr const T& get() const;

        // get a pointer to the type or null if the type isn't correct
        template<typename T>
        constexpr T* get_if();
        template<typename T>
        constexpr const T* get_if() const;
    };
} // namespace cc

#include <common/variant.inl>
