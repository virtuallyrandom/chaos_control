#pragma once

#include <unordered_map>

namespace cc
{
    template <class Key, class Type, class Hasher = std::hash<Key>, class KeyEq = std::equal_to<Key>, class Alloc = std::allocator<std::pair<const Key, Type>>>
    class unordered_map : public std::unordered_map<Key, Type, Hasher, KeyEq, Alloc>
    {
    public:
        size_t length() const
        {
            return std::unordered_map<Key, Type, Hasher, KeyEq, Alloc>::size();
        }

    private:
        size_t size() const;
    };
} // namespace cc
