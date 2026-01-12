#include <containers/freelist.h>

namespace cc
{
    freelist::freelist(size_t const elementSize, size_t const elementCount, byte* const buffer)
        : m_count(elementCount)
        , m_hiBit(bit_ceil(elementCount + 1))  // count b00000101 -> b000001000
        , m_abaShift(countr_zero(m_hiBit))     // hibit b00001000 -> 3
        , m_abaMask(SIZE_MAX << m_abaShift)    // abaShift 3 -> 0xfffffffffffffff8
        , m_indexBytes((m_abaShift + 7ull) / 8ull)   // abaShift 3 -> (3+7)/8 -> 1
        , m_stride(max(m_indexBytes, elementSize))
        , m_storage(buffer)
   {
        clear();
    }

    void* freelist::acquire()
    {
        size_t cmp = m_freeIndex.load();
        for (;;)
        {
            size_t const index = cmp & ~m_abaMask;

            // reached the end of the list
            if (index == m_count)
                return nullptr;

            size_t const nextAba = (cmp + (1ull << m_abaShift)) & m_abaMask;

            size_t next;
            memcpy(&next, m_storage + index * m_stride, m_indexBytes);

            if (m_freeIndex.compare_exchange_weak(cmp, nextAba | next))
                return m_storage + index * m_stride;
        }
    }

    void freelist::release(void* const obj)
    {
        if (obj == nullptr)
            return;

        rw_barrier();

        intptr_t const storageOffset = reinterpret_cast<byte*>(obj) - m_storage;
        size_t const storageIndex = storageOffset / m_stride;

        assert(storageOffset >= 0);

        size_t cmp = m_freeIndex.load();
        for (;;)
        {
            size_t const index = cmp & ~m_abaMask;
            memcpy(m_storage + storageOffset, &index, m_indexBytes);
            size_t const freeIndex = ((cmp + (1ull << m_abaShift)) & m_abaMask) | storageIndex;
            if (m_freeIndex.compare_exchange_weak(cmp, freeIndex))
                break;
        }
    }

    void freelist::clear()
    {
        m_freeIndex = 0;

        for (size_t i = 0; i < m_count; i++)
        {
            size_t const next = i + 1;
            memcpy(m_storage + i * m_stride, &next, m_indexBytes);
        }
    }
} // namespace cc
