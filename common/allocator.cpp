#include <common/allocator.h>
#include <common/atomic.h>
#include <common/math.h>
#include <common/mutex.h>

namespace cc
{
    allocator::allocator()
    {
        if (&get_root_allocator() == this)
            return;

        m_parent = &get_root_allocator().find_allocator(this);

        unique_lock lock(m_parent->m_children_lock);
        m_next = m_parent->m_children;
        m_parent->m_children = this;
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
            allocator_page* const t = m_pages;
            m_pages = m_pages->next;
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
        allocator_page* page = m_pages;
        while (page)
        {
            if (head >= page->head && tail <= page->tail)
                return true;
            page = page->next;
        }

        return false;
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

    void allocator::add_page(void const* const ptr, size_t const length)
    {
        allocator_page* const page = new(get_root_allocator()) allocator_page;
        if (page == nullptr)
            return;

        page->head = reinterpret_cast<uintptr_t>(ptr);
        page->tail = page->head + length;

        unique_lock lock(m_page_lock);

        if (m_pages == nullptr)
            m_pages = page;
        else
        {
            allocator_page* t = m_pages;
            while (nullptr != t->next)
                t = page->next;

            t->next = page;
        }

        m_page_min = min(m_page_min, page->head);
        m_page_max = max(m_page_max, page->tail);
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
