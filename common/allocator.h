#pragma once

#include <common/memory.h>

namespace cc
{
    class scope_allocator
    {
    public:
        scope_allocator(class allocator&);
        ~scope_allocator();
    };

    class allocator
    {
    public:
        allocator();
        virtual ~allocator();

        // allocate at least SIZE bytes of memory and copy the contents of the
        // original pointer (if not null) into it. alignment must be a power of two
        // or empty to use the implementation's default alignment.
        //
        // if size is empty:
        //   - if PTR is not null, it is deallocated and nullptr is returned.
        //   - if PTR is null, a unique pointer is returned.
        //
        // if the allocation fails, returns nullptr and the original pointer remains
        // valid.
        void* reallocate(void* ptr, size_t size, align_val_t = {}) noexcept;

        // allocate at least SIZE bytes. alignment must be a power of two or empty
        // to use the implementation's default alignment. allocating zero bytes
        // returns a unique pointer. returns nullptr if the allocation fails.
        void* allocate(size_t size, align_val_t align = {}) noexcept;

        // free memory from the current heap. pointer may be nullptr.
        void deallocate(void* ptr) noexcept;

        template <typename Type, class... Args>
        Type* construct() noexcept;

        template <typename Type>
        void destroy(Type*);

        // determine if the current heap owns the memory. returns true on success,
        // false, otherwise.
        bool owns(void const*, size_t size = {}) const noexcept;

        // obtain the usable memory size of the pointer passed.
        size_t size(void const*) const noexcept;

        // find the child-most allocator that contains this pointer. 
        allocator& find_allocator(void const*) noexcept;
        allocator const& find_allocator(void const*) const noexcept;

    protected:
        // add a page of memory to the allocator. this is used to
        // determine if an block of memory is within the allocator.
        void add_page(void const*, size_t);

        virtual void* internal_reallocate(void*, size_t size, align_val_t) noexcept = 0;
        virtual size_t internal_size(void const*) const noexcept = 0;

    private:
        compiler_disable_copymove(allocator);

        struct allocator_page
        {
            uintptr_t head{};
            uintptr_t tail{};
            allocator_page* next{};
        };

        allocator& find_owning_child(void const*) noexcept;
        allocator const& find_owning_child(void const*) const noexcept;

        mutable shared_timed_mutex m_page_lock;
        uintptr_t m_page_min{ UINTPTR_MAX };
        uintptr_t m_page_max{};
        allocator_page* m_pages{};

        atomic<size_t> m_allocs{};
        atomic<size_t> m_frees{};
        shared_timed_mutex m_children_lock;
        allocator* m_parent{};
        allocator* m_children{}; // head of child list; iterate next until null
        allocator* m_next{}; // next sibling
    };
} // namespace cc

#include <common/allocator.inl>
