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

        // determine if the current heap owns the memory. returns true on success,
        // false, otherwise.
        bool owns(void const*, size_t size = {}) const noexcept;

        // obtain the usable memory size of the pointer passed.
        size_t size(void const*) const noexcept;

        // obtain the total capacity of the allocator, which is the summation of
        // all of its pages.
        size_t capacity() const noexcept;

        // obtain the total used memory of the allocator.
        size_t used() const noexcept;

        // obtain how many bytes are available in the allocator.
        size_t available() const noexcept;

        // find the child-most allocator that contains this pointer. 
        allocator& find_allocator(void const*) noexcept;
        allocator const& find_allocator(void const*) const noexcept;

        size_t num_allocs() const { return m_allocs; }
        size_t num_frees() const { return m_frees; }

    protected:
        compiler_disable_copymove(allocator);

        // page information is stored in the page buffer provided when adding a page.
        // it is stored at the end of the page (aligned) thus:
        //   [ u s a b l e m e m o r y [page]]
        // if a page has the flag page::kDynamic, it is owned by the parent allocator.
        // adding a page does not grant availability of the entire space as
        // ~sizeof(page) is reserved for management information.
        // size() may return less than it's actual size due to alignment.
        struct page
        {
            enum flag
            {
                kNone = 0,
                kDynamic = 1 << 0,
            };

            uintptr_t head{}; // start of usable area
            uintptr_t tail{}; // end of usable area (+1)
            uint64_t flags { kNone };
            page* next{};
        };

        allocator() = delete;
        allocator(void*, size_t, allocator&);
        allocator(size_t, align_val_t, allocator&);
        allocator(size_t, align_val_t);

        allocator& find_owning_child(void const*) noexcept;
        allocator const& find_owning_child(void const*) const noexcept;

        // add a page of memory to the allocator. this is used to
        // determine if an block of memory is within the allocator.
        void add_page(page::flag, void*, size_t);

        virtual void* internal_reallocate(void*, size_t size, align_val_t) noexcept = 0;
        virtual size_t internal_size(void const*) const noexcept = 0;
        virtual size_t internal_used() const noexcept = 0;

        uintptr_t m_page_min{ UINTPTR_MAX };
        uintptr_t m_page_max{};
        page* m_pages{};

        atomic<size_t> m_allocs{};
        atomic<size_t> m_frees{};
        allocator* m_parent{};
        allocator* m_children{}; // head of child list; iterate next until null
        allocator* m_next{}; // next sibling

        mutable shared_timed_mutex m_page_lock;
        shared_timed_mutex m_children_lock;
    };
} // namespace cc

#include <common/allocator.inl>
