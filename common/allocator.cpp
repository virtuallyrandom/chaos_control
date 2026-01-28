#include <common/align.h>
#include <common/allocator.h>
#include <common/atomic.h>
#include <common/math.h>
#include <common/mutex.h>

namespace cc
{
    allocator::allocator(void* const buffer, size_t const buffer_size, allocator& alloc)
    {
        if (&allocator_root() == this)
            return;

        assert(nullptr != buffer);

        m_parent = &alloc;

        {
            unique_lock lock(m_parent->m_children_lock);
            m_next = m_parent->m_children;
            m_parent->m_children = this;
        }

        add_page(page::kNone, buffer, buffer_size);
    }

    allocator::allocator(size_t const buffer_size, align_val_t const align, allocator& alloc)
    {
        if (&allocator_root() == this)
            return;

        assert(0 != buffer_size);

        m_parent = &alloc;

        {
            unique_lock lock(m_parent->m_children_lock);
            m_next = m_parent->m_children;
            m_parent->m_children = this;
        }

        void* const buffer = m_parent->allocate(buffer_size, align);
        assert(nullptr != buffer);
        add_page(page::kDynamic, buffer, buffer_size);
    }

    allocator::allocator(size_t const buffer_size, align_val_t const align)
    {
        if (&allocator_root() == this)
            return;

        assert(0 != buffer_size);

        m_parent = &allocator_top();

        {
            unique_lock lock(m_parent->m_children_lock);
            m_next = m_parent->m_children;
            m_parent->m_children = this;
        }

        void* const buffer = m_parent->allocate(buffer_size, align);
        assert(nullptr != buffer);
        add_page(page::kDynamic, buffer, buffer_size);
    }

    allocator::~allocator()
    {
        if (nullptr != m_parent)
        {
            unique_lock lock(m_parent->m_children_lock);
            scope_allocator scope(*m_parent);
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
        }

        while (nullptr != m_pages)
        {
            page* const t = m_pages;
            m_pages = m_pages->next;
            if ((t->flags & page::kDynamic) && nullptr != m_parent)
                m_parent->deallocate(reinterpret_cast<void*>(t->head));
            delete t;
        }

        // dangling children
        assert(m_children == nullptr);
    }

    bool allocator::owns(void const* const ptr, size_t const length) const noexcept
    {
        if (m_pages == nullptr)
            return true;

        uintptr_t const head = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t const tail = head + length;

        if (tail < m_page_min || head > m_page_max)
            return false;

        shared_lock lock(m_page_lock);
        page* pg = m_pages;
        while (pg)
        {
            if (head >= pg->head && tail <= pg->tail)
                return true;
            pg = pg->next;
        }

        return false;
    }

    size_t allocator::capacity() const noexcept
    {
        size_t total{};
        page const* pg = m_pages;
        while (nullptr != pg)
        {
            total += pg->tail - pg->head;
            pg = pg->next;
        }
        return total;
    }

    size_t allocator::used() const noexcept
    {
        return internal_used();
    }

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

        return find_owning_child(ptr);
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

        return find_owning_child(ptr);
    }

    void allocator::add_page(page::flag const flg, void* const ptr, size_t const buffer_size)
    {
        page* const pg = new (allocator_root()) page;
        if (nullptr == pg)
        {
            assert(nullptr != pg);
            return;
        }

        pg->head  = reinterpret_cast<uintptr_t>(ptr);
        pg->tail  = pg->head + buffer_size;
        pg->flags = flg;

        unique_lock lock(m_page_lock);

        if (m_pages == nullptr)
            m_pages = pg;
        else
        {
            page* t = m_pages;
            while (nullptr != t->next)
                t = pg->next;

            t->next = pg;
        }

        m_page_min = min(m_page_min, pg->head);
        m_page_max = max(m_page_max, pg->tail);
    }

    allocator& allocator::find_owning_child(void const* const ptr) noexcept
    {
        for (allocator* child = m_children; child != nullptr; child = child->m_next)
        {
            if (child->owns(ptr))
                return child->find_owning_child(ptr);
        }

        return *this;
    }

    allocator const& allocator::find_owning_child(void const* const ptr) const noexcept
    {
        for (allocator const* child = m_children; child != nullptr; child = child->m_next)
        {
            if (child->owns(ptr))
                return child->find_owning_child(ptr);
        }

        return *this;
    }

} // namespace cc
