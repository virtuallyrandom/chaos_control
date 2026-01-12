#pragma once

namespace cc
{
    template <typename Type, size_t Count>
    template <class... Args>
    Type* static_freelist<Type, Count>::acquire(Args&&... args)
    {
        void* const obj = freelist::acquire();
        if (obj == nullptr)
            return nullptr;

        return new(obj) Type(cc::forward<Args>(args)...);
    }

    template <typename Type, size_t Count>
    void static_freelist<Type, Count>::release(Type* const obj)
    {
        if (obj == nullptr)
            return;

        obj->~Type();

        freelist::release(obj);
    }

    template <typename Type, size_t Count>
    void static_freelist<Type, Count>::clear()
    {
        // oh this is fun...
        // it doesn't know what's outstanding, so it can't call destructors
        freelist::clear();
    }
} // namespace cc
