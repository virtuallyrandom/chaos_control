#pragma once

#include <vector>

namespace cc
{
    // identical to std::vector, except std::vector::size is replaced
    // with cc::vector::count as 'size' is misused.
    template<class Type, class Alloc = std::allocator<Type>>
    class vector : public std::vector<Type, Alloc>
    {
    public:
        size_t length() const
        {
            return std::vector<Type, Alloc>::size();
        }
    private:
        size_t size() const;
    };

} // namespace cc
