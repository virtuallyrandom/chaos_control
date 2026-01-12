#pragma once

namespace cc
{
    template<class... Args>
    template<typename T>
    constexpr variant<Args...>::operator T& ()
    {
        assert(holds<T>());
        return std::get<T>(*this);
    }

    template<class... Args>
    template<typename T>
    constexpr variant<Args...>::operator const T& () const
    {
        assert(holds<T>());
        return std::get<T>(*this);
    }

    template<class... Args>
    template<typename T>
    constexpr bool variant<Args...>::holds() const
    {
        return std::holds_alternative<T>(*this);
    }

    template<class... Args>
    template<typename T>
    constexpr T& variant<Args...>::get()
    {
        assert(holds<T>());
        return *this;
    }

    template<class... Args>
    template<typename T>
    constexpr const T& variant<Args...>::get() const
    {
        assert(holds<T>());
        return *this;
    }

    template<class... Args>
    template<typename T>
    constexpr T* variant<Args...>::get_if()
    {
        if (holds<T>())
            return &(T&)*this;
        return nullptr;
    }

    template<class... Args>
    template<typename T>
    constexpr const T* variant<Args...>::get_if() const
    {
        if (holds<T>())
            return &(T&)*this;
        return nullptr;
    }
} // namespace cc
