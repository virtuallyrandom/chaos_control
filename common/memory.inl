#pragma once

#include <common/memory.h>

namespace cc
{
    template <typename T>
    T* object_allocator<T>::allocate(std::size_t n)
    {
        if (auto p = static_cast<T*>(::operator new(n * sizeof(T))))
        {
            const char* const str = typeid(p).name();
            (void)str;
            return p;
        }
        throw std::bad_alloc();
    }

    template <typename T>
    void object_allocator<T>::deallocate(T* p, std::size_t)
    {
        ::operator delete(p);
    }

} // namespace cc
