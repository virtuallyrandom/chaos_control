#pragma once

#include <common/string.h>
#include <common/types.h>

struct hash128
{
    uint64_t value[2]{ UINT64_MAX, UINT64_MAX };
    int operator<=>(hash128 const& o) const
    {
        return memcmp(value, o.value, sizeof(value));
    }
};

#if 0
using hash64 = uint64_t;
#else
struct hash64
{
public:
    hash64() = default;

    hash64(hash64 const&) noexcept = default;
    hash64& operator=(hash64 const&) noexcept = default;

    hash64(hash64&&) noexcept = default;
    hash64& operator=(hash64&&) noexcept = default;

    explicit hash64(uint64_t const hv) noexcept : m_value(hv) {}

    int operator<=>(hash64 const& o) const noexcept
    {
        if (m_value > o.m_value)
            return 1;
        if (m_value < o.m_value)
            return -1;
        return 0;
    }

    int operator<=>(uint64_t const& o) const noexcept
    {
        if (m_value > o)
            return 1;
        if (m_value < o)
            return -1;
        return 0;
    }

    uint64_t get() const { return m_value; }
    operator uint64_t() const noexcept { return m_value; }

private:
    uint64_t m_value{ UINT64_MAX };
};
#endif // 0

struct hash32
{
    uint32_t value{ UINT32_MAX };
    int operator<=>(hash32 const& o) const
    {
        if (value > o.value)
            return 1;
        if (value < o.value)
            return -1;
        return 0;
    }
};

constexpr hash128 kdHash128Empty{};
constexpr hash64 kHash64Empty{};
constexpr hash32 kHash32Empty{};

namespace cc
{
    inline hash128 hash128_spookyv2(void const*, size_t size, hash128 from={});
    inline hash64 hash64_spookyv2(void const*, size_t size, hash64 from={});
    inline hash32 hash32_spookyv2(void const*, size_t size, hash32 from={});

    template<typename Type, size_t Length>
    inline constexpr hash128 hash128_spookyv2(Type const (&buffer)[Length], hash128 from);

    template<typename Type, size_t Length>
    inline constexpr hash128 hash128_spookyv2(Type const (&buffer)[Length]);

    template<typename Type, size_t Length>
    inline constexpr hash64 hash64_spookyv2(Type const (&buffer)[Length], hash64 from);

    template<typename Type, size_t Length>
    inline constexpr hash64 hash64_spookyv2(Type const (&buffer)[Length]);

    template<typename Type, size_t Length>
    inline constexpr hash32 hash32_spookyv2(Type const (&buffer)[Length], hash32 from);

    template<typename Type, size_t Length>
    inline constexpr hash32 hash32_spookyv2(Type const (&buffer)[Length]);
} // namespace cc

inline hash128 operator ""_hash128(char const*, size_t length);
inline hash64 operator ""_hash64(char const*, size_t length);
inline hash32 operator ""_hash32(char const*, size_t length);


#include <common/hash.inl>