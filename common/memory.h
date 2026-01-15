#pragma once

#include <cstdint>
#include <memory>
#include <new>

#include <common/atomic.h>
#include <common/compiler.h>
#include <common/mutex.h>
#include <containers/vector.h>

namespace cc
{
    class allocator;

    using std::destroy_at;

    using std::unique_ptr;
    using std::make_unique;

    using std::align_val_t;

    // the object allocator calls new/delete to allocate counts of a type.
    // it is used by containers and is not a general purpose allocator.
    template <typename T>
    class object_allocator
    {
    public:
        using value_type = T;

        object_allocator() = default;

        template <typename U>
        object_allocator(const object_allocator<U>&) {}

        T* allocate(std::size_t n);
        void deallocate(T* p, std::size_t);
    };

    void push_allocator(allocator&);
    void pop_allocator();

    allocator& get_root_allocator();

    allocator& find_allocator(void const*);
} // namespace cc

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, cc::allocator&);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, std::align_val_t);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, std::align_val_t, cc::allocator&);

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, cc::allocator&, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, std::align_val_t, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t count, std::align_val_t, cc::allocator&, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, cc::allocator&);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, std::align_val_t);

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, cc::allocator&, std::align_val_t);

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, cc::allocator&, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, std::align_val_t, const std::nothrow_t&) noexcept;

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t count, std::align_val_t, cc::allocator&, const std::nothrow_t&) noexcept;

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
