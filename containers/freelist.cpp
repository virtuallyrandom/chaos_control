#include <containers/freelist.h>

namespace cc
{
    freelist::freelist(size_t const m_element_size, size_t const element_count, byte* const buffer)
        : m_count(element_count)
        , m_hi_bit(bit_ceil(element_count + 1))  // count b00000101 -> b000001000
        , m_aba_shift(countr_zero(m_hi_bit))     // hibit b00001000 -> 3
        , m_aba_mask(SIZE_MAX << m_aba_shift)    // abaShift 3 -> 0xfffffffffffffff8
        , m_index_bytes((m_aba_shift + 7ull) / 8ull)   // abaShift 3 -> (3+7)/8 -> 1
        , m_stride(max(m_index_bytes, m_element_size))
        , m_storage(buffer)
   {
        clear();
    }

    void* freelist::acquire()
    {
        size_t cmp = m_free_index.load();
        for (;;)
        {
            size_t const index = cmp & ~m_aba_mask;

            // reached the end of the list
            if (index == m_count)
                return nullptr;

            size_t const next_aba = (cmp + (1ull << m_aba_shift)) & m_aba_mask;

            size_t next;
            memcpy(&next, m_storage + index * m_stride, m_index_bytes);

            if (m_free_index.compare_exchange_weak(cmp, next_aba | next))
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

        size_t cmp = m_free_index.load();
        for (;;)
        {
            size_t const index = cmp & ~m_aba_mask;
            memcpy(m_storage + storageOffset, &index, m_index_bytes);
            size_t const freeIndex = ((cmp + (1ull << m_aba_shift)) & m_aba_mask) | storageIndex;
            if (m_free_index.compare_exchange_weak(cmp, freeIndex))
                break;
        }
    }

    void freelist::clear()
    {
        m_free_index = 0;

        for (size_t i = 0; i < m_count; i++)
        {
            size_t const next = i + 1;
            memcpy(m_storage + i * m_stride, &next, m_index_bytes);
        }
    }
} // namespace cc
