#pragma once

#include <common/compiler.h>
#include <common/types.h>

namespace cc::utility
{
    class processor_info
    {
    public:
        enum class cache_type : uint8_t
        {
            kUnified,
            kInstruction,
            kData,
            kTrace,
            kInvalid,
        };

        struct cache
        {
            uint8_t associativity = 0;
            cache_type type;
            uint16_t lineSize = 0;
            uint32_t size = 0;
            uint64_t logicalCoreMask = 0;
        };

        processor_info();
        ~processor_info();

        size_t getComplexCount() const;
        size_t getPhysicalCoreCount(size_t const complexIndex = SIZE_MAX) const;
        size_t getLogicalCoreCount(size_t const coreIndex = SIZE_MAX, size_t const complexIndex = SIZE_MAX) const;

        size_t getCacheCount(size_t const level) const;
        cache getCache(size_t const level, size_t const index);

    private:
        compiler_disable_copymove(processor_info);

        struct complex_info;
        struct core_info;
        struct numa_info;
        struct cache_info;

        complex_info* m_complexInfo = nullptr;
        size_t m_complexCount = 0;

        core_info* m_coreInfo = nullptr;
        size_t m_coreCount = 0;

        numa_info* m_numaInfo = nullptr;
        size_t m_numaCount = 0;

        cache_info* m_cacheInfo = nullptr;
        size_t m_cacheCount = 0;
    };
} // namespace cc::utility
