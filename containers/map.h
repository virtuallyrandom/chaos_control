#pragma once

#include <map>

namespace cc
{
    template <class Key, class Type, class Less = std::less<Key>, class Alloc = std::allocator<std::pair<const Key, Type>>>
    class map : public std::map<Key, Type, Less, Alloc>
    {
    public:
        size_t length() const
        {
            return std::map<Key, Type, Less, Alloc>::size();
        }

    private:
        size_t size() const;
    };
} // namespace cc
