#pragma once

#include <set>

#include <common/memory.h>

namespace cc
{
    template <typename Type>
    class passthrough_allocator;

    template <class Key, class CmpPred=std::less<Key>, class Alloc=cc::passthrough_allocator<Key>>
    class set : public std::set<Key, CmpPred, Alloc>
    {
    public:
        size_t length() const
        {
            return std::set<Key, CmpPred, Alloc>::size();
        }

    private:
        size_t size() const;

    };
//    using std::set;
} // namespace cc
