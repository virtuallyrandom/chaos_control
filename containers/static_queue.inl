#pragma once

namespace cc
{
    template <typename Type, size_t Count>
    template <class... Args>
    Type* static_queue<Type, Count>::write_acquire(Args&&... args)
    {
        Type* const value = reinterpret_cast<Type*>(queue::write_acquire());
        if (value != nullptr)
            new (value) Type(cc::forward<Args>(args)...);
        return value;
    }

    template <typename Type, size_t Count>
    void static_queue<Type, Count>::write_release(Type* const ptr)
    {
        // this function only exists for api completeness
        queue::write_release(ptr);
    }

    template <typename Type, size_t Count>
    Type* static_queue<Type, Count>::read_acquire()
    {
        return reinterpret_cast<Type*>(queue::read_acquire());
    }

    template <typename Type, size_t Count>
    void static_queue<Type, Count>::read_release(Type* const ptr)
    {
        if (ptr != nullptr)
            ptr->~Type();
        queue::read_release(ptr);
    }

    template <typename Type, size_t Count>
    template <class... Args>
    bool static_queue<Type, Count>::push(Args&&... args)
    {
        Type* const t = write_acquire(forward<Args>(args)...);
        if (t != nullptr)
            write_release(t);
        return t != nullptr;
    }

    template <typename Type, size_t Count>
    bool static_queue<Type, Count>::pop(Type* const t)
    {
        assert(t != nullptr);

        Type* const tp = read_acquire();
        if (tp == nullptr)
            return false;

        *t = *tp;

        read_release(tp);

        return true;
    }

    template <typename Type, size_t Count>
    void static_queue<Type, Count>::clear()
    {
        // cause destructors for outstanding data
        for (;;)
        {
            Type* const ptr = read_acquire();
            if (ptr == nullptr)
                break;
            read_release(ptr);
        }
        queue::clear();
    }
} // namespace cc
