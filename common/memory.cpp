#include <common/memory.h>
#include <common/platform/platform_memory.h>

#include <malloc.h>

namespace
{
    static constexpr size_t kAllocatorStackDepth{ 32 };
    static constexpr cc::align_val_t kDefaultAlignment{ 8 };

    thread_local static cc::allocator* s_allocatorStack[kAllocatorStackDepth]{};
    thread_local static size_t s_allocatorStackDepth{};

    static cc::root_allocator s_root;
} // namespace [anonymous]

namespace cc
{
    void allocator_push(allocator& a)
    {
        assert(s_allocatorStackDepth != kAllocatorStackDepth);
        s_allocatorStack[s_allocatorStackDepth++] = &a;
    }

    void allocator_pop()
    {
        assert(s_allocatorStackDepth != 0);
        s_allocatorStackDepth--;
    }

    cc::allocator& cc::allocator_top()
    {
        if (s_allocatorStackDepth != 0)
            return *s_allocatorStack[s_allocatorStackDepth - 1];
        return s_root;
    }

    cc::allocator& allocator_root()
    {
        return s_root;
    }

    allocator& find_allocator(void const* ptr)
    {
        return s_root.find_allocator(ptr);
    }

    scope_allocator::scope_allocator(allocator& a)
    {
        allocator_push(a);
    }

    scope_allocator::~scope_allocator()
    {
        allocator_pop();
    }
} // namespace cc

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size)
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, cc::allocator& alloc)
{
    return alloc.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::align_val_t const align)
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::align_val_t const align, cc::allocator& alloc)
{
    return alloc.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, cc::allocator& alloc, std::nothrow_t const&) noexcept
{
    return alloc.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::align_val_t align, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::align_val_t align, cc::allocator & alloc, std::nothrow_t const&) noexcept
{
    return alloc.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size)
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::align_val_t const align, cc::allocator& alloc)
{
    return alloc.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, cc::allocator& alloc)
{
    return alloc.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, cc::allocator& alloc, std::nothrow_t const&) noexcept
{
    return alloc.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::align_val_t const align, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    return top.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::align_val_t const align, cc::allocator& alloc, std::nothrow_t const&) noexcept
{
    return alloc.allocate(size, align);
}

void operator delete(void* const ptr) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, std::align_val_t const) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, size_t const) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, size_t, std::align_val_t const) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, std::align_val_t const, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, std::align_val_t const) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, size_t const) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, size_t const, std::align_val_t const) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, std::align_val_t const, std::nothrow_t const&) noexcept
{
    cc::allocator& top = cc::allocator_top();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}
