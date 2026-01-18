#pragma once

#include <common/hash.h>
#include <common/string.h>

namespace
{
    // source code based on spooky2 implementation

    static constexpr size_t sc_numVars = 12;
    static constexpr size_t sc_blockSize = sc_numVars * 8;
    static constexpr size_t sc_bufSize = 2 * sc_blockSize;
    static constexpr uint64_t sc_const = 0xdeadbeefdeadbeefLL;

    static constexpr inline uint64_t Rot64(uint64_t x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }

    static constexpr inline void Mix(
        const uint64_t* data,
        uint64_t& s0, uint64_t& s1, uint64_t& s2, uint64_t& s3,
        uint64_t& s4, uint64_t& s5, uint64_t& s6, uint64_t& s7,
        uint64_t& s8, uint64_t& s9, uint64_t& s10, uint64_t& s11)
    {
        s0 += data[0];    s2 ^= s10;    s11 ^= s0;    s0 = Rot64(s0, 11);    s11 += s1;
        s1 += data[1];    s3 ^= s11;    s0 ^= s1;    s1 = Rot64(s1, 32);    s0 += s2;
        s2 += data[2];    s4 ^= s0;    s1 ^= s2;    s2 = Rot64(s2, 43);    s1 += s3;
        s3 += data[3];    s5 ^= s1;    s2 ^= s3;    s3 = Rot64(s3, 31);    s2 += s4;
        s4 += data[4];    s6 ^= s2;    s3 ^= s4;    s4 = Rot64(s4, 17);    s3 += s5;
        s5 += data[5];    s7 ^= s3;    s4 ^= s5;    s5 = Rot64(s5, 28);    s4 += s6;
        s6 += data[6];    s8 ^= s4;    s5 ^= s6;    s6 = Rot64(s6, 39);    s5 += s7;
        s7 += data[7];    s9 ^= s5;    s6 ^= s7;    s7 = Rot64(s7, 57);    s6 += s8;
        s8 += data[8];    s10 ^= s6;    s7 ^= s8;    s8 = Rot64(s8, 55);    s7 += s9;
        s9 += data[9];    s11 ^= s7;    s8 ^= s9;    s9 = Rot64(s9, 54);    s8 += s10;
        s10 += data[10];    s0 ^= s8;    s9 ^= s10;    s10 = Rot64(s10, 22);    s9 += s11;
        s11 += data[11];    s1 ^= s9;    s10 ^= s11;    s11 = Rot64(s11, 46);    s10 += s0;
    }

    static constexpr inline void EndPartial(
        uint64_t& h0, uint64_t& h1, uint64_t& h2, uint64_t& h3,
        uint64_t& h4, uint64_t& h5, uint64_t& h6, uint64_t& h7,
        uint64_t& h8, uint64_t& h9, uint64_t& h10, uint64_t& h11)
    {
        h11 += h1;    h2 ^= h11;   h1 = Rot64(h1, 44);
        h0 += h2;    h3 ^= h0;    h2 = Rot64(h2, 15);
        h1 += h3;    h4 ^= h1;    h3 = Rot64(h3, 34);
        h2 += h4;    h5 ^= h2;    h4 = Rot64(h4, 21);
        h3 += h5;    h6 ^= h3;    h5 = Rot64(h5, 38);
        h4 += h6;    h7 ^= h4;    h6 = Rot64(h6, 33);
        h5 += h7;    h8 ^= h5;    h7 = Rot64(h7, 10);
        h6 += h8;    h9 ^= h6;    h8 = Rot64(h8, 13);
        h7 += h9;    h10 ^= h7;    h9 = Rot64(h9, 38);
        h8 += h10;   h11 ^= h8;    h10 = Rot64(h10, 53);
        h9 += h11;   h0 ^= h9;    h11 = Rot64(h11, 42);
        h10 += h0;    h1 ^= h10;   h0 = Rot64(h0, 54);
    }

    static constexpr inline void End(
        const uint64_t* data,
        uint64_t& h0, uint64_t& h1, uint64_t& h2, uint64_t& h3,
        uint64_t& h4, uint64_t& h5, uint64_t& h6, uint64_t& h7,
        uint64_t& h8, uint64_t& h9, uint64_t& h10, uint64_t& h11)
    {
        h0 += data[0];   h1 += data[1];   h2 += data[2];   h3 += data[3];
        h4 += data[4];   h5 += data[5];   h6 += data[6];   h7 += data[7];
        h8 += data[8];   h9 += data[9];   h10 += data[10]; h11 += data[11];
        EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
        EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
        EndPartial(h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
    }

    static constexpr inline void ShortMix(uint64_t& h0, uint64_t& h1, uint64_t& h2, uint64_t& h3)
    {
        h2 = Rot64(h2, 50);  h2 += h3;  h0 ^= h2;
        h3 = Rot64(h3, 52);  h3 += h0;  h1 ^= h3;
        h0 = Rot64(h0, 30);  h0 += h1;  h2 ^= h0;
        h1 = Rot64(h1, 41);  h1 += h2;  h3 ^= h1;
        h2 = Rot64(h2, 54);  h2 += h3;  h0 ^= h2;
        h3 = Rot64(h3, 48);  h3 += h0;  h1 ^= h3;
        h0 = Rot64(h0, 38);  h0 += h1;  h2 ^= h0;
        h1 = Rot64(h1, 37);  h1 += h2;  h3 ^= h1;
        h2 = Rot64(h2, 62);  h2 += h3;  h0 ^= h2;
        h3 = Rot64(h3, 34);  h3 += h0;  h1 ^= h3;
        h0 = Rot64(h0, 5);   h0 += h1;  h2 ^= h0;
        h1 = Rot64(h1, 36);  h1 += h2;  h3 ^= h1;
    }

    static constexpr inline void ShortEnd(uint64_t& h0, uint64_t& h1, uint64_t& h2, uint64_t& h3)
    {
        h3 ^= h2;  h2 = Rot64(h2, 15);  h3 += h2;
        h0 ^= h3;  h3 = Rot64(h3, 52);  h0 += h3;
        h1 ^= h0;  h0 = Rot64(h0, 26);  h1 += h0;
        h2 ^= h1;  h1 = Rot64(h1, 51);  h2 += h1;
        h3 ^= h2;  h2 = Rot64(h2, 28);  h3 += h2;
        h0 ^= h3;  h3 = Rot64(h3, 9);   h0 += h3;
        h1 ^= h0;  h0 = Rot64(h0, 47);  h1 += h0;
        h2 ^= h1;  h1 = Rot64(h1, 54);  h2 += h1;
        h3 ^= h2;  h2 = Rot64(h2, 32);  h3 += h2;
        h0 ^= h3;  h3 = Rot64(h3, 25);  h0 += h3;
        h1 ^= h0;  h0 = Rot64(h0, 63);  h1 += h0;
    }

    hash128 Short(const void* message, size_t length, hash128 seed)
    {
        union
        {
            const uint8_t* p8;
            uint32_t* p32;
            uint64_t* p64;
            size_t i;
        } u;

        u.p8 = (const uint8_t*)message;

        size_t remainder = length % 32;
        uint64_t a = seed.value[0];
        uint64_t b = seed.value[1];
        uint64_t c = sc_const;
        uint64_t d = sc_const;

        if (length > 15)
        {
            const uint64_t* end = u.p64 + (length / 32) * 4;

            // handle all complete sets of 32 bytes
            for (; u.p64 < end; u.p64 += 4)
            {
                c += u.p64[0];
                d += u.p64[1];
                ShortMix(a, b, c, d);
                a += u.p64[2];
                b += u.p64[3];
            }

            //Handle the case of 16+ remaining bytes.
            if (remainder >= 16)
            {
                c += u.p64[0];
                d += u.p64[1];
                ShortMix(a, b, c, d);
                u.p64 += 2;
                remainder -= 16;
            }
        }

        // Handle the last 0..15 bytes, and its length
        d += ((uint64_t)length) << 56;
        switch (remainder)
        {
            case 15:
                d += ((uint64_t)u.p8[14]) << 48;
                [[fallthrough]];
            case 14:
                d += ((uint64_t)u.p8[13]) << 40;
                [[fallthrough]];
            case 13:
                d += ((uint64_t)u.p8[12]) << 32;
                [[fallthrough]];
            case 12:
                d += u.p32[2];
                c += u.p64[0];
                break;
            case 11:
                d += ((uint64_t)u.p8[10]) << 16;
                [[fallthrough]];
            case 10:
                d += ((uint64_t)u.p8[9]) << 8;
                [[fallthrough]];
            case 9:
                d += (uint64_t)u.p8[8];
                [[fallthrough]];
            case 8:
                c += u.p64[0];
                break;
            case 7:
                c += ((uint64_t)u.p8[6]) << 48;
                [[fallthrough]];
            case 6:
                c += ((uint64_t)u.p8[5]) << 40;
                [[fallthrough]];
            case 5:
                c += ((uint64_t)u.p8[4]) << 32;
                [[fallthrough]];
            case 4:
                c += u.p32[0];
                break;
            case 3:
                c += ((uint64_t)u.p8[2]) << 16;
                [[fallthrough]];
            case 2:
                c += ((uint64_t)u.p8[1]) << 8;
                [[fallthrough]];
            case 1:
                c += (uint64_t)u.p8[0];
                break;
            case 0:
                c += sc_const;
                d += sc_const;
                break;
        }
        ShortEnd(a, b, c, d);
        return hash128{ { a, b } };
    }
} // namespace anonymous

namespace cc
{
    inline hash128 hash128_spookyv2(void const* buffer, size_t size, hash128 from)
    {
        if (size < sc_bufSize)
            return Short(buffer, size, from);

        uint64_t h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11;
        uint64_t buf[sc_numVars];
        uint64_t* end;
        union
        {
            const uint8_t* p8;
            uint64_t* p64;
            size_t i;
        } u;
        size_t remainder;

        h0 = h3 = h6 = h9 = from.value[0];
        h1 = h4 = h7 = h10 = from.value[1];
        h2 = h5 = h8 = h11 = sc_const;

        u.p8 = (const uint8_t*)buffer;
        end = u.p64 + (size / sc_blockSize) * sc_numVars;

        // handle all whole sc_blockSize blocks of bytes
        if ((u.i & 0x7) == 0)
        {
            while (u.p64 < end)
            {
                Mix(u.p64, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
                u.p64 += sc_numVars;
            }
        }
        else
        {
            while (u.p64 < end)
            {
                memcpy(buf, u.p64, sc_blockSize);
                Mix(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
                u.p64 += sc_numVars;
            }
        }

        // handle the last partial block of sc_blockSize bytes
        remainder = (size - ((const uint8_t*)end - (const uint8_t*)buffer));
        memcpy(buf, end, remainder);
        memset(((uint8_t*)buf) + remainder, 0, sc_blockSize - remainder);
        ((uint8_t*)buf)[sc_blockSize - 1] = static_cast<uint8_t>(remainder);

        // do some final mixing 
        End(buf, h0, h1, h2, h3, h4, h5, h6, h7, h8, h9, h10, h11);
        return hash128{ { h0, h1 } };
    }

    inline hash64 hash64_spookyv2(void const* const buffer, size_t const size, hash64 from)
    {
        hash128 const from128{ { from, from } };
        hash128 const result = hash128_spookyv2(buffer, size, from128);
        return hash64{ result.value[0] };
    }

    inline hash32 hash32_spookyv2(void const* const buffer, size_t size, hash32 from)
    {
        hash64 const from64{ (static_cast<uint64_t>(from.value) << 32) | from.value};
        hash128 const from128{ { from64, from64 }  };
        return hash32{ static_cast<uint32_t>(hash128_spookyv2(buffer, size, from128).value[0]) };
    }

    template<typename Type, size_t Length>
    inline constexpr hash128 hash128_spookyv2(Type const (&buffer)[Length], hash128 from)
    {
        return hash128_spookyv2(buffer, sizeof(Type) * Length, from);
    }

    template<typename Type, size_t Length>
    inline constexpr hash128 hash128_spookyv2(Type const (&buffer)[Length])
    {
        return hash128_spookyv2(buffer, sizeof(Type) * Length, hash128{});
    }

    template<typename Type, size_t Length>
    inline constexpr hash64 hash64_spookyv2(Type const (&buffer)[Length], hash64 from)
    {
        return hash64_spookyv2(buffer, sizeof(Type) * Length, from);
    }

    template<typename Type, size_t Length>
    inline constexpr hash64 hash64_spookyv2(Type const (&buffer)[Length])
    {
        return hash64_spookyv2(buffer, sizeof(Type) * Length, hash64{});
    }

    template<typename Type, size_t Length>
    inline constexpr hash32 hash32_spookyv2(Type const (&buffer)[Length], hash32 from)
    {
        return hash32_spookyv2(buffer, sizeof(Type) * Length, from);
    }

    template<typename Type, size_t Length>
    inline constexpr hash32 hash32_spookyv2(Type const (&buffer)[Length])
    {
        return hash32_spookyv2(buffer, sizeof(Type) * Length, hash32{});
    }
} // namespace cc

inline hash128 operator ""_hash128(char const* const string, size_t const length)
{
    return cc::hash128_spookyv2(string, length, hash128{});
}

inline hash64 operator ""_hash64(char const* const string, size_t const length)
{
    return cc::hash64_spookyv2(string, length, hash64{});
}

inline hash32 operator ""_hash32(char const* const string, size_t const length)
{
    return cc::hash32_spookyv2(string, length, hash32{});
}
