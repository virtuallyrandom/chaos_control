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

        [[nodiscard]]void* acquire();

        void release(void* const);

        virtual void clear();

    protected:
        freelist(size_t const elementSize, size_t const elementCount, byte* const buffer);

        static constexpr size_t required_size(size_t const elementSize, size_t const elementCount)
        {
            size_t const hiBit = bit_ceil(elementCount + 1); // count b00000101 -> b000001000
            int const abaShift = countr_zero(hiBit);      // hibit b00001000 -> 3
            size_t const indexBytes = (abaShift + 7ull) / 8ull;    // abaShift 3 -> (3+7)/8 -> 1
            size_t const stride = max(indexBytes, elementSize);
            return stride * elementCount;
        }

    private:
        freelist(freelist const&) = delete;
        freelist(freelist&&) = delete;
        freelist& operator=(freelist const&) = delete;
        freelist& operator=(freelist&&) = delete;

        decl_align(hardware_destructive_interference_size) atomic<size_t> m_freeIndex;
        size_t  const m_count;
        size_t  const m_hiBit;
        int32_t const m_pad{};
        int32_t const m_abaShift;
        size_t  const m_abaMask;
        size_t  const m_indexBytes;
        size_t  const m_stride;
        byte*   const m_storage;
    };
} // namespace cc
