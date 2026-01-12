#pragma once

#include <cstdint>
#include <memory>
#include <new>

#include <common/compiler.h>
#include <common/mutex.h>
#include <containers/vector.h>

namespace cc
{
    using std::destroy_at;

    using std::unique_ptr;
    using std::make_unique;

    using std::align_val_t;

    class scope_allocator
    {
    public:
        scope_allocator(class allocator&);
        ~scope_allocator();
    };

    template <typename T>
    class passthrough_allocator;

    class allocator
    {
    public:
        allocator();
        virtual ~allocator();

        virtual void* allocate(size_t size, align_val_t align) noexcept = 0;
        virtual void deallocate(void*) noexcept = 0;
        virtual bool owns(void const*) const noexcept = 0;

        allocator& find_allocator(void const*) noexcept;
        allocator const& find_allocator(void const*) const noexcept;

    private:
        compiler_disable_copymove(allocator);

        allocator* find_owning_child(void const*) noexcept;
        allocator const* find_owning_child(void const*) const noexcept;

        cc::shared_timed_mutex m_children_lock;
        allocator* m_parent{};
        allocator* m_children{}; // head of child list; iterate next until null
        allocator* m_next{}; // next sibling
    };

    template <typename T>
    class passthrough_allocator
    {
    public:
        using value_type = T;

        passthrough_allocator() = default;

        template <typename U>
        passthrough_allocator(const passthrough_allocator<U>&) {}

        T* allocate(std::size_t n)
        {
            if (auto p = static_cast<T*>(::operator new(n * sizeof(T))))
            {
                const char* const str = typeid(p).name();
                (void)str;
                return p;
            }
            throw std::bad_alloc();
        }

        void deallocate(T* p, std::size_t)
        {
            ::operator delete(p);
        }
    };

    void push_allocator(allocator&);
    void pop_allocator();

    allocator& find_allocator(void const*);
} // namespace cc

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, std::align_val_t);

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, std::align_val_t, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, std::align_val_t);

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, std::align_val_t, const std::nothrow_t&) noexcept;

void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, std::align_val_t) noexcept;
void operator delete(void* ptr, size_t sz) noexcept;
void operator delete(void* ptr, size_t sz, std::align_val_t) noexcept;
void operator delete(void* ptr, const std::nothrow_t&) noexcept;
void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept;

void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, std::align_val_t) noexcept;
void operator delete[](void* ptr, size_t sz) noexcept;
void operator delete[](void* ptr, size_t sz, std::align_val_t) noexcept;
void operator delete[](void* ptr, const std::nothrow_t&) noexcept;
void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept;

#include <common/memory.inl>
