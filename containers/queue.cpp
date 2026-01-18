#include <containers/queue.h>

#include <common/assert.h>
#include <common/bit.h>
#include <common/buildmsg.h>

BUILDMSG_FIXME("this needs to move to a freelist impl and have the queue use that");

namespace cc {
    queue::queue(size_t const elem_size, size_t const elem_count, void* const buffer, size_t const bufferSize)
    {
        assert(buffer != nullptr);
        assert(bufferSize > sizeof(impl));

        assert(bufferSize >= required_size(elem_size, elem_count));

        byte* ptr = reinterpret_cast<byte*>(buffer);

        me = new (ptr) impl();
        ptr += sizeof(impl);

        me->m_free_list = reinterpret_cast<atomic<size_t>*>(ptr);
        ptr += freelist_size(elem_count);

        me->m_storage = ptr;
        ptr += storage_size(elem_size, elem_count);

        me->m_element_list = reinterpret_cast<atomic<void*>*>(ptr);
        ptr += element_size(elem_count);

        me->m_element_count = elem_count;
        me->m_element_size = elem_size;

        for (uint32_t i = 0; i < elem_count; ++i)
            me->m_element_list[ i ].store(nullptr);

        clear();
    }

    void* queue::write_acquire() {
        if (me == nullptr)
            return nullptr;

        size_t const low_mask = me->m_element_count - 1;
        int const high_shift = countr_zero(me->m_element_count) + 1;
        size_t const invalid_bit = size_t(1) << (high_shift - 1);

        for (;;)
        {
            size_t cmp = me->m_free_next.load();
            if (cmp & invalid_bit)
                return nullptr;
            size_t const cmp_index = cmp & low_mask;
            size_t const cmp_uid = (size_t)((uint32_t)cmp>> high_shift);
            size_t const set = ((cmp_uid + 1) <<high_shift) | (me->m_free_list[cmp_index] & (low_mask | invalid_bit));

            if (me->m_free_next.compare_exchange_weak(cmp, set))
                return me->m_storage + cmp_index * me->m_element_size;
        }
    }

    void queue::write_release(void* const ptr)
    {
        if (me == nullptr)
            return;

        if (ptr == nullptr)
            return;

        rw_barrier();

        [[maybe_unused]] size_t const was = me->m_push_available--;
        assert(was > 0);

        size_t const full_index = me->m_push_index++;

        // stall until the previous value is consumed
        for(;;)
        {
            atomic<void*>& element = me->m_element_list[full_index & (me->m_element_count - 1)];
            void* tmp = nullptr;
            if (element.compare_exchange_weak(tmp, ptr))
                break;
        }

        // record that data is now available to be consumed
        me->m_pop_available++;
    }

    void* queue::read_acquire()
    {
        if (me == nullptr)
            return nullptr;

        // reserve a count
        for (;;)
        {
            size_t cmp = me->m_pop_available;
            if (cmp == 0)
                return nullptr;

            if (me->m_pop_available.compare_exchange_weak(cmp, cmp - 1))
                break;
        }

        // get the index
        size_t const fullIndex = me->m_pop_index++;
        size_t const wrap_index = fullIndex & (me->m_element_count - 1);

        atomic<void*>& element = me->m_element_list[wrap_index];
        // wait for the object to become valid while also attempting to clear it
        for (;;)
        {
            void* const ptr = element.exchange(nullptr);
            if (ptr != nullptr)
            {
                // put an available push count
                me->m_push_available++;
                return ptr;
            }
        }
    }

    void queue::read_release(void* const ptr)
    {
        if (me == nullptr)
            return;

        if (ptr == nullptr)
            return;

        size_t const low_mask = me->m_element_count - 1;
        int const high_shift = countr_zero(me->m_element_count) + 1;
        size_t const invalid_bit = size_t(1) << (high_shift - 1);

        // return it to the free list
        uintptr_t const offset = (uintptr_t)ptr - (uintptr_t)me->m_storage;
        size_t const index = size_t{ offset / me->m_element_size };
        assert(index < me->m_element_count); // not owned by this queue

        for (;;)
        {
            size_t cmp = me->m_free_next.load();
            size_t const cmp_index = cmp & (low_mask | invalid_bit);
            size_t const cmp_uid = (size_t)((uint32_t)cmp >> high_shift);
            size_t const set = ((cmp_uid + 1) << high_shift) | index;
            me->m_free_list[index] = cmp_index;
            if (me->m_free_next.compare_exchange_weak(cmp, set))
                break;
        }
    }

    void queue::clear()
    {
        if (me == nullptr)
            return;

        me->m_push_index = 0;
        me->m_pop_index = 0;
        me->m_pop_available = 0;
        me->m_push_available = me->m_element_count;
        me->m_free_next = 0;

        for (size_t i = 0; i < me->m_element_count; ++i)
            me->m_free_list[i] = i + 1;

        me->m_free_list[me->m_element_count - 1] = SIZE_MAX;
    }

    bool queue::empty() const
    {
        return me->m_pop_available == 0;
    }
} // namespace cc
