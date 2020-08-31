// Copyright (C) 2012 Codership Oy <info@codership.com>

/*!
 * @file
 *
 * This header file defines FNV hash functions for 3 hash sizes:
 * 4, 8 and 16 bytes.
 *
 * Be wary of bitshift multiplication "optimization" (FNV_BITSHIFT_OPTIMIZATION):
 * FNV authors used to claim marginal speedup when using it, however on core2
 * CPU it has shown no speedup for fnv32a and more than 2x slowdown for fnv64a
 * and fnv128a. Disabled by default.
 *
 * FNV vs. FNVa: FNVa has a better distribution: multiplication happens after
 *               XOR and hence propagates XOR effect to all bytes of the hash.
 *               Hence by default functions perform FNVa. GU_FNV_NORMAL macro
 *               is needed for unit tests.
 *
 * gu_fnv*_internal() functions are endian-unsafe, their output should be
 * converted to little-endian format if it is to be exported to other machines.
 */

#ifndef _gu_fnv_h_
#define _gu_fnv_h_

#include "gu_int128.h"

#include <stdint.h>
#include <stdlib.h> // ssize_t
#include <assert.h>

#define GU_FNV32_PRIME 16777619UL
#define GU_FNV32_SEED  2166136261UL

#if !defined(GU_FNVBITSHIFT_OPTIMIZATION)
#  define GU_FNV32_MUL(_x) _x *= GU_FNV32_PRIME
#else /* GU_FNVBITSHIFT_OPTIMIZATION */
#  define GU_FNV32_MUL(_x)                                              \
    _x += (_x << 1) + (_x << 4) + (_x << 7) + (_x << 8) + (_x << 24)
#endif /* GU_FNVBITSHIFT_OPTIMIZATION */

#if !defined(GU_FNV_NORMAL)
#  define GU_FNV32_ITERATION(_s,_b) _s ^= _b; GU_FNV32_MUL(_s);
#else
#  define GU_FNV32_ITERATION(_s,_b) GU_FNV32_MUL(_s); _s ^= _b;
#endif

static GU_FORCE_INLINE void
gu_fnv32a_internal (const void* buf, ssize_t const len, uint32_t* seed)
{
    const uint8_t* bp = (const uint8_t*)buf;
    const uint8_t* const be = bp + len;

    while (bp + 2 <= be)
    {
        GU_FNV32_ITERATION(*seed,*bp++);
        GU_FNV32_ITERATION(*seed,*bp++);
    }

    if (bp < be)
    {
        GU_FNV32_ITERATION(*seed,*bp++);
    }

    assert(be == bp);
}

#define GU_FNV64_PRIME 1099511628211ULL
#define GU_FNV64_SEED  14695981039346656037ULL

#if !defined(GU_FNVBITSHIFT_OPTIMIZATION)
#  define GU_FNV64_MUL(_x) _x *= GU_FNV64_PRIME
#else /* GU_FNVBITSHIFT_OPTIMIZATION */
#  define GU_FNV64_MUL(_x)                                              \
    _x +=(_x << 1) + (_x << 4) + (_x << 5) + (_x << 7) + (_x << 8) + (_x << 40);
#endif /* GU_FNVBITSHIFT_OPTIMIZATION */

#if !defined(GU_FNV_NORMAL)
#  define GU_FNV64_ITERATION(_s,_b) _s ^= _b; GU_FNV64_MUL(_s);
#else
#  define GU_FNV64_ITERATION(_s,_b) GU_FNV64_MUL(_s); _s ^= _b;
#endif

static GU_FORCE_INLINE void
gu_fnv64a_internal (const void* buf, ssize_t const len, uint64_t* seed)
{
    const uint8_t* bp = (const uint8_t*)buf;
    const uint8_t* const be = bp + len;

    while (bp + 2 <= be)
    {
        GU_FNV64_ITERATION(*seed,*bp++);
        GU_FNV64_ITERATION(*seed,*bp++);
    }

    if (bp < be)
    {
        GU_FNV64_ITERATION(*seed,*bp++);
    }

    assert(be == bp);
}

static gu_uint128_t const
GU_SET128(GU_FNV128_PRIME, 0x0000000001000000ULL, 0x000000000000013BULL);

static gu_uint128_t const
GU_SET128(GU_FNV128_SEED,  0x6C62272E07BB0142ULL, 0x62B821756295C58DULL);

#if defined(__SIZEOF_INT128__)

#define GU_FNV128_XOR(_s,_b) _s ^= _b

#if !defined(GU_FNVBITSHIFT_OPTIMIZATION)
#  define GU_FNV128_MUL(_x) _x *= GU_FNV128_PRIME
#else /* GU_FNVBITSHIFT_OPTIMIZATION */
#  define GU_FNV128_MUL(_x)                                             \
    _x +=(_x << 1) + (_x << 3) + (_x << 4) + (_x << 5) + (_x << 8) + (_x << 88);
#endif /* GU_FNVBITSHIFT_OPTIMIZATION */

#else /* ! __SIZEOF_INT128__ */

#define GU_FNV128_XOR(_s,_b) (_s).u32[GU_32LO] ^= _b

#if defined(GU_FNV128_FULL_MULTIPLICATION)
#  define GU_FNV128_MUL(_x) GU_MUL128_INPLACE(_x, GU_FNV128_PRIME)
#else /* no FULL_MULTIPLICATION */
#  define GU_FNV128_MUL(_x) {                                           \
        uint32_t carry =                                                \
            (((_x).u64[GU_64LO] & 0x00000000ffffffffULL) * 0x013b) >> 32; \
        carry = (((_x).u64[GU_64LO] >> 32) * 0x013b + carry)   >> 32;   \
        (_x).u64[GU_64HI] *= 0x013b;                                    \
        (_x).u64[GU_64HI] += ((_x).u64[GU_64LO] << 24) + carry;         \
        (_x).u64[GU_64LO] *= 0x013b;                                    \
    }
#endif /* FULL_MULTIPLICATION */

#endif /* ! __SIZEOF_INT128__ */


#if !defined(GU_FNV_NORMAL)
#  define GU_FNV128_ITERATION(_s,_b) GU_FNV128_XOR(_s,_b); GU_FNV128_MUL(_s);
#else
#  define GU_FNV128_ITERATION(_s,_b) GU_FNV128_MUL(_s); GU_FNV128_XOR(_s,_b);
#endif

inline static void
gu_fnv128a_internal (const void* buf, ssize_t const len, gu_uint128_t* seed)
{
    const uint8_t* bp = (const uint8_t*)buf;
    const uint8_t* const be = bp + len;

    /* this manual loop unrolling seems to be essential */
    while (bp + 8 <= be)
    {
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
    }

    if (bp + 4 <= be)
    {
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
    }

    if (bp + 2 <= be)
    {
        GU_FNV128_ITERATION(*seed, *bp++);
        GU_FNV128_ITERATION(*seed, *bp++);
    }

    if (bp < be)
    {
        GU_FNV128_ITERATION(*seed, *bp++);
    }

    assert(be == bp);
}

#endif /* _gu_fnv_h_ */

