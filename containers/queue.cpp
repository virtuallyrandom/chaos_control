#include <containers/queue.h>

#include <common/assert.h>
#include <common/bit.h>
#include <common/buildmsg.h>

BUILDMSG_FIXME("this needs to move to a freelist impl and have the queue use that");

namespace cc {
    queue::queue(size_t const elementSize, size_t const elementCount, void* const buffer, size_t const bufferSize)
    {
        assert(buffer != nullptr);
        assert(bufferSize > sizeof(impl));

        assert(bufferSize >= required_size(elementSize, elementCount));

        byte* ptr = reinterpret_cast<byte*>(buffer);

        me = new (ptr) impl();
        ptr += sizeof(impl);

        me->freeList = reinterpret_cast<atomic<size_t>*>(ptr);
        ptr += freelist_size(elementCount);

        me->storage = ptr;
        ptr += storage_size(elementSize, elementCount);

        me->elementList = reinterpret_cast<atomic<void*>*>(ptr);
        ptr += element_size(elementCount);

        me->elementCount = elementCount;
        me->elementSize = elementSize;

        for (uint32_t i = 0; i < elementCount; ++i)
            me->elementList[ i ].store(nullptr);

        clear();
    }

    void* queue::write_acquire() {
        if (me == nullptr)
            return nullptr;

        size_t const LOW_MASK = me->elementCount - 1;
        int const HIGH_SHIFT = countr_zero(me->elementCount) + 1;
        size_t const INVALID_BIT = size_t(1) << (HIGH_SHIFT - 1);

        for (;;)
        {
            size_t cmp = me->freeNext.load();
            if (cmp & INVALID_BIT)
                return nullptr;
            size_t const cmpIndex = cmp & LOW_MASK;
            size_t const cmpUID = (size_t)((uint32_t)cmp>> HIGH_SHIFT);
            size_t const set = ((cmpUID + 1) <<HIGH_SHIFT) | (me->freeList[cmpIndex] & (LOW_MASK | INVALID_BIT));

            if (me->freeNext.compare_exchange_weak(cmp, set))
                return me->storage + cmpIndex * me->elementSize;
        }
    }

    void queue::write_release(void* const ptr)
    {
        if (me == nullptr)
            return;

        if (ptr == nullptr)
            return;

        rw_barrier();

        [[maybe_unused]] size_t const was = me->pushAvail--;
        assert(was > 0);

        size_t const fullIndex = me->pushIndex++;

        // stall until the previous value is consumed
        for(;;)
        {
            atomic<void*>& element = me->elementList[fullIndex & (me->elementCount - 1)];
            void* tmp = nullptr;
            if (element.compare_exchange_weak(tmp, ptr))
                break;
        }

        // record that data is now available to be consumed
        me->popAvail++;
    }

    void* queue::read_acquire()
    {
        if (me == nullptr)
            return nullptr;

        // reserve a count
        for (;;)
        {
            size_t cmp = me->popAvail;
            if (cmp == 0)
                return nullptr;

            if (me->popAvail.compare_exchange_weak(cmp, cmp - 1))
                break;
        }

        // get the index
        size_t const fullIndex = me->popIndex++;
        size_t const wrapIndex = fullIndex & (me->elementCount - 1);

        atomic<void*>& element = me->elementList[wrapIndex];
        // wait for the object to become valid while also attempting to clear it
        for (;;)
        {
            void* const ptr = element.exchange(nullptr);
            if (ptr != nullptr)
            {
                // put an available push count
                me->pushAvail++;
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

        size_t const LOW_MASK = me->elementCount - 1;
        int const HIGH_SHIFT = countr_zero(me->elementCount) + 1;
        size_t const INVALID_BIT = size_t(1) << (HIGH_SHIFT - 1);

        // return it to the free list
        uintptr_t const offset = (uintptr_t)ptr - (uintptr_t)me->storage;
        size_t const index = size_t{ offset / me->elementSize };
        assert(index < me->elementCount); // not owned by this queue

        for (;;)
        {
            size_t cmp = me->freeNext.load();
            size_t const cmpIndex = cmp & (LOW_MASK | INVALID_BIT);
            size_t const cmpUID = (size_t)((uint32_t)cmp >> HIGH_SHIFT);
            size_t const set = ((cmpUID + 1) << HIGH_SHIFT) | index;
            me->freeList[index] = cmpIndex;
            if (me->freeNext.compare_exchange_weak(cmp, set))
                break;
        }
    }

    void queue::clear()
    {
        if (me == nullptr)
            return;

        me->pushIndex = 0;
        me->popIndex = 0;
        me->popAvail = 0;
        me->pushAvail = me->elementCount;
        me->freeNext = 0;

        for (size_t i = 0; i < me->elementCount; ++i)
            me->freeList[i] = i + 1;
        me->freeList[me->elementCount - 1] = SIZE_MAX;
    }

    bool queue::isEmpty() const
    {
        return me->popAvail == 0;
    }
} // namespace cc
