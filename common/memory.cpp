#include <common/atomic.h>
#include <common/memory.h>
#include <common/mutex.h>

#include <malloc.h>

namespace
{
    static constexpr size_t kAllocatorStackDepth{ 32 };
    static constexpr cc::align_val_t kDefaultAlignment{ 8 };

    thread_local static cc::allocator* s_allocatorStack[kAllocatorStackDepth]{};
    thread_local static size_t s_allocatorStackDepth{};

    class root_allocator final : public cc::allocator
    {
    public:
        root_allocator() = default;
        virtual ~root_allocator() = default;

        virtual void* allocate(size_t const sizeRaw, cc::align_val_t const alignVal) noexcept override
        {
            size_t const align = static_cast<size_t>(alignVal);
            assert((align & (align - 1)) == 0); // not a power of two

            // some implementations fail if the requested size
            // isn't a multiple of the alignment.
            size_t const size = (sizeRaw + (align - 1)) & ~(align - 1);
            void* const ptr = _aligned_malloc(size, align);
            assert(nullptr != ptr);
            return ptr;
        }

        virtual void deallocate(void* const ptr) noexcept override
        {
            _aligned_free(ptr);
        }

        virtual bool owns(void const*) const noexcept override
        {
            return true;
        }

        compiler_disable_copymove(root_allocator);
    };

    static root_allocator s_root;

    cc::allocator& top_allocator()
    {
        if (s_allocatorStackDepth != 0)
            return *s_allocatorStack[s_allocatorStackDepth - 1];
        return s_root;
    }
} // namespace [anonymous]

namespace cc
{
    allocator::allocator()
    {
        if (&s_root == this)
            return;

        m_parent = &s_root.find_allocator(this);

        unique_lock lock(m_parent->m_children_lock);
        scope_allocator scope(s_root);
        m_next = m_parent->m_children;
        m_parent->m_children = this;
//        m_parent->m_children.push_back(this); // vector
//        m_parent->m_children.insert(this); // set
    }

    allocator::~allocator()
    {
        if (nullptr != m_parent)
        {
            unique_lock lock(m_parent->m_children_lock);
            scope_allocator scope(s_root);
            if (m_parent->m_children == this)
                m_parent->m_children = m_next;
            else
            {
                allocator* prev = m_parent->m_children;
                while (prev->m_next)
                {
                    if (prev->m_next == this)
                    {
                        prev->m_next = m_next;
                        break;
                    }
                    prev = prev->m_next;
                }
            }
            // vector
//            for (size_t i = 0; i < m_children.length(); i++)
//            {
//                if (m_children[i] == this)
//                {
//                    m_children.erase(m_children.begin() + static_cast<ptrdiff_t>(i));
//                    break;
//                }
//            }
//            m_parent->m_children.erase(this); // set
        }

        // dangling children
        assert(m_children == nullptr);
//        assert(m_children.empty());
    }

#if 0
    void* allocator::allocate(size_t size) noexcept
    {
        return allocate(size, kDefaultAlignment);
    }

    void allocator::deallocate(void* const ptr, size_t) noexcept
    {
        deallocate(ptr);
    }
#endif // 0

    allocator& allocator::find_allocator(void const* const ptr) noexcept
    {
        // the root allocator owns all remaining memory, so either
        // this is the root and owns the memory, or it is not and
        // it has a valid parent.
        if (!owns(ptr))
        {
            assert(nullptr != m_parent);
            return m_parent->find_allocator(ptr);
        }

        allocator* const child = find_owning_child(ptr);
        return child ? *child : *this;
    }

    allocator const& allocator::find_allocator(void const* const ptr) const noexcept
    {
        // the root allocator owns all remaining memory, so either
        // this is the root and owns the memory, or it is not and
        // it has a valid parent.
        if (!owns(ptr))
        {
            assert(nullptr != m_parent);
            return m_parent->find_allocator(ptr);
        }

        allocator const* const child = find_owning_child(ptr);
        return child ? *child : *this;
    }

    allocator* allocator::find_owning_child(void const* const ptr) noexcept
    {
//        for (allocator* child : m_children)
        for (allocator* child = m_children; child != nullptr; child = child->m_next)
        {
            if (child->owns(ptr))
                return child->find_owning_child(ptr);
        }

        return nullptr;
    }

    allocator const* allocator::find_owning_child(void const* const ptr) const noexcept
    {
//        for (allocator const* child : m_children)
        for (allocator const* child = m_children; child != nullptr; child = child->m_next)
        {
            if (child->owns(ptr))
                return child->find_owning_child(ptr);
        }

        return nullptr;
    }

    void push_allocator(allocator& a)
    {
        assert(s_allocatorStackDepth != kAllocatorStackDepth);
        s_allocatorStack[s_allocatorStackDepth++] = &a;
    }

    void pop_allocator()
    {
        assert(s_allocatorStackDepth != 0);
        s_allocatorStackDepth--;
    }

    allocator& find_allocator(void const* ptr)
    {
        return s_root.find_allocator(ptr);
    }

    scope_allocator::scope_allocator(allocator& a)
    {
        push_allocator(a);
    }

    scope_allocator::~scope_allocator()
    {
        pop_allocator();
    }
} // namespace cc

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size)
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::align_val_t const align)
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new(size_t const size, std::align_val_t align, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size)
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_notnull_ _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::align_val_t const align)
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, align);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, kDefaultAlignment);
}

_VCRT_EXPORT_STD _NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(count) _VCRT_ALLOCATOR
void* operator new[](size_t const size, std::align_val_t const align, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    return top.allocate(size, align);
}

void operator delete(void* const ptr) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, std::align_val_t const) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, size_t const) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, size_t, std::align_val_t const) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete(void* const ptr, std::align_val_t const, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, std::align_val_t const) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, size_t const) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, size_t const, std::align_val_t const) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}

void operator delete[](void* const ptr, std::align_val_t const, std::nothrow_t const&) noexcept
{
    cc::allocator& top = top_allocator();
    cc::allocator& owner = top.find_allocator(ptr);
    owner.deallocate(ptr);
}
