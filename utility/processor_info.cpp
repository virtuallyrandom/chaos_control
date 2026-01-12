#include <utility/processor_info.h>

#include <common/assert.h>
#include <common/bit.h>
#include <common/math.h>
#include <common/memory.h>
#include <common/platform/windows.h>

namespace cc::utility
{
    struct processor_info::complex_info
    {
        ULONG_PTR processorMask = 0;
    };

    struct processor_info::core_info
    {
        ULONG_PTR processorMask = 0;
    };

    struct processor_info::numa_info
    {
        ULONG_PTR processorMask = 0;
    };

    struct processor_info::cache_info
    {
        ULONG_PTR processorMask = 0;
        uint8_t level = 0;
        uint8_t associativity = 0;
        cache_type type = cache_type::kInvalid;
        uint8_t pad0 = 0;
        uint16_t lineSize = 0;
        uint16_t pad1 = 0;
        uint32_t size = 0;
        uint32_t pad2 = 0;
    };

    processor_info::processor_info()
    {
        BOOL (WINAPI* getLogicalProcessorInfo)(void*, PDWORD);

        HMODULE const module = GetModuleHandleA("kernel32");
        if (module == nullptr)
        {
            assert(module != nullptr);
            return;
        }

        void* const fnp = GetProcAddress(module, "GetLogicalProcessorInformation");
        if (fnp == nullptr)
            return;

        memcpy(&getLogicalProcessorInfo, &fnp, sizeof(fnp));

        SYSTEM_LOGICAL_PROCESSOR_INFORMATION* slpi = nullptr;
        DWORD count = 0;
        DWORD length = 0;

        constexpr size_t slpiSize = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

        for (;;)
        {
            BOOL const rv = getLogicalProcessorInfo(slpi, &length);

            assert(length % slpiSize == 0);

            if (rv)
                break;

            assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

            delete[] slpi;

            count = length / slpiSize;
            slpi = new SYSTEM_LOGICAL_PROCESSOR_INFORMATION[count];
        }

        for (size_t i = 0; i < count; i++)
        {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION& pi = slpi[i];

            switch (pi.Relationship)
            {
                case RelationNumaNode:
                    m_numaCount++;
                    break;

                case RelationProcessorCore:
                    m_coreCount++;
                    break;

                case RelationCache:
                    m_cacheCount++;
                    break;

                case RelationProcessorPackage:
                    m_complexCount++;
                    break;

                case RelationAll:
                case RelationGroup:
                case RelationProcessorDie:
                case RelationNumaNodeEx:
                case RelationProcessorModule:
                default:
                    break;
            }
        }

        m_complexInfo = new complex_info[m_complexCount];
        m_coreInfo = new core_info[m_coreCount];
        m_numaInfo = new numa_info[m_numaCount];
        m_cacheInfo = new cache_info[m_cacheCount];

        // reparse to assign logical cores to their respective cores and numa nodes
        size_t complexIndex = 0;
        size_t coreIndex = 0;
        size_t numaIndex = 0;
        size_t cacheIndex = 0;
        for (size_t i = 0; i < count; i++)
        {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION& pi = slpi[i];

            if (pi.Relationship == RelationNumaNode)
            {
                numa_info& info = m_numaInfo[numaIndex++];
                info.processorMask = pi.ProcessorMask;
            }

            else if (pi.Relationship == RelationProcessorPackage)
            {
                complex_info& info = m_complexInfo[complexIndex++];
                info.processorMask = pi.ProcessorMask;
            }

            else if (pi.Relationship == RelationProcessorCore)
            {
                core_info& info = m_coreInfo[coreIndex++];
                info.processorMask = pi.ProcessorMask;
            }

            else if (pi.Relationship == RelationCache)
            {
                cache_info& cacheInfo = m_cacheInfo[cacheIndex++];
                cacheInfo.level = pi.Cache.Level;
                cacheInfo.associativity = pi.Cache.Associativity;
                cacheInfo.lineSize = pi.Cache.LineSize;
                cacheInfo.processorMask = pi.ProcessorMask;
                cacheInfo.size = pi.Cache.Size;

                constexpr cache_type map[] =
                {
                   cache_type::kUnified,     // CacheUnified,
                   cache_type::kInstruction, // CacheInstruction,
                   cache_type::kData,        // CacheData,
                   cache_type::kTrace        // CacheTrace
                };
                cacheInfo.type = map[pi.Cache.Type];
            }
        }

        delete[] slpi;
    }

    processor_info::~processor_info()
    {
        delete[] m_complexInfo;
        delete[] m_coreInfo;
        delete[] m_numaInfo;
    }

    size_t processor_info::getComplexCount() const
    {
        return m_complexCount;
    }

    size_t processor_info::getPhysicalCoreCount(size_t const complexIndex) const
    {
        if (complexIndex == SIZE_MAX)
            return m_coreCount;

        else
        {
            size_t count = 0;
            assert(complexIndex < m_complexCount);
            complex_info const& complexInfo = m_complexInfo[complexIndex];
            for (size_t i = 0; i < m_coreCount; i++)
            {
                if ((m_coreInfo[i].processorMask & complexInfo.processorMask) != 0)
                    count++;
            }
            return count;
        }
    }

    size_t processor_info::getLogicalCoreCount(size_t const coreIndex, size_t const complexIndex) const
    {
        size_t count = 0;
        if (coreIndex == SIZE_MAX)
        {
            if (complexIndex == SIZE_MAX)
            {
                for (size_t i = 0; i < m_coreCount; i++)
                    count += popcount(m_coreInfo[i].processorMask);
            }
            else
            {
                assert(complexIndex < m_complexCount);
                count += popcount(m_complexInfo[complexIndex].processorMask);
            }
        }
        else
        {
            if (complexIndex == SIZE_MAX)
            {
                assert(coreIndex < m_coreCount);
                count += popcount(m_coreInfo[coreIndex].processorMask);
            }
            else
            {
                assert(complexIndex < m_complexCount);
                complex_info const& complexInfo = m_complexInfo[complexIndex];
                for (size_t i = 0; i < m_coreCount; i++)
                    count += popcount(m_coreInfo[i].processorMask & complexInfo.processorMask);
            }
        }
        return count;
    }

    size_t processor_info::getCacheCount(size_t const level) const
    {
        size_t count = 0;
        for (size_t i = 0; i < m_cacheCount; i++)
        {
            cache_info const& cacheInfo = m_cacheInfo[i];
            if (cacheInfo.level == level)
                count++;
        }
        return count;
    }

    processor_info::cache processor_info::getCache(size_t const level, size_t const index)
    {
        size_t cacheIndex = 0;
        for (size_t i = 0; i < m_cacheCount; i++)
        {
            cache_info const& cacheInfo = m_cacheInfo[i];
            if (cacheInfo.level == level && cacheIndex++ == index)
            {
                cache c;
                c.associativity = cacheInfo.associativity;
                c.lineSize = cacheInfo.lineSize;
                c.logicalCoreMask = cacheInfo.processorMask;
                c.size = cacheInfo.size;
                c.type = cacheInfo.type;
                return c;
            }
        }

        return cache();
    }
} // namespace cc::utility
