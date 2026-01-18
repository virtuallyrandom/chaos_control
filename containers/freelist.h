#pragma once

#include <common/atomic.h>
#include <common/compiler.h>
#include <common/bit.h>
#include <common/concurrency.h>
#include <common/math.h>
#include <common/types.h>

namespace cc
{
    class freelist
    {
    public:
        virtual ~freelist() = default;

        [[nodiscard]] void* acquire();

        void release(void* const);

        virtual void clear();

    protected:
        freelist(size_t const m_element_size, size_t const element_count, byte* const buffer);

        static constexpr size_t required_size(size_t const element_size, size_t const element_count)
        {
            size_t const hiBit = bit_ceil(element_count + 1); // count b00000101 -> b000001000
            int const abaShift = countr_zero(hiBit);      // hibit b00001000 -> 3
            size_t const indexBytes = (abaShift + 7ull) / 8ull;    // abaShift 3 -> (3+7)/8 -> 1
            size_t const stride = max(indexBytes, element_size);
            return stride * element_count;
        }

    private:
        freelist(freelist const&) = delete;
        freelist(freelist&&) = delete;
        freelist& operator=(freelist const&) = delete;
        freelist& operator=(freelist&&) = delete;

        decl_align(hardware_destructive_interference_size) atomic<size_t> m_free_index;
        size_t  const m_count;
        size_t  const m_hi_bit;
        int32_t const m_pad{};
        int32_t const m_aba_shift;
        size_t  const m_aba_mask;
        size_t  const m_index_bytes;
        size_t  const m_stride;
        byte*   const m_storage;
    };
} // namespace cc
