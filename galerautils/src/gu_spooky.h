// Copyright (C) 2012-2017 Codership Oy <info@codership.com>

/*!
 * @file Spooky hash by Bob Jenkins:
 *       http://www.burtleburtle.net/bob/c/spooky.h
 *
 * Original author comments preserved in C++ style.
 * Original code is public domain
 *
 * $Id$
 */

#ifndef _gu_spooky_h_
#define _gu_spooky_h_

#include "gu_types.h"
#include "gu_byteswap.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h> // for memcpy()

/*! GCC complains about 'initializer element is not constant', hence macros */
#define _spooky_numVars   12
#define _spooky_blockSize 96  /* (_spooky_numVars * 8)   */
#define _spooky_bufSize   192 /* (_spooky_blockSize * 2) */
static uint64_t const _spooky_const = GU_ULONG_LONG(0xDEADBEEFDEADBEEF);

//
// This is used if the input is 96 bytes long or longer.
//
// The internal state is fully overwritten every 96 bytes.
// Every input bit appears to cause at least 128 bits of entropy
// before 96 other bytes are combined, when run forward or backward
//   For every input bit,
//   Two inputs differing in just that input bit
//   Where "differ" means xor or subtraction
//   And the base value is random
//   When run forward or backwards one Mix
// I tried 3 pairs of each; they all differed by at least 212 bits.
//
static GU_FORCE_INLINE void _spooky_mix(
    const uint64_t *data,
    uint64_t* s0, uint64_t* s1, uint64_t* s2, uint64_t* s3,
    uint64_t* s4, uint64_t* s5, uint64_t* s6, uint64_t* s7,
    uint64_t* s8, uint64_t* s9, uint64_t* sA, uint64_t* sB)
{
    *s0 += gu_le64(data[0]);  *s2 ^= *sA; *sB ^= *s0; *s0 =GU_ROTL64(*s0,11); *sB += *s1;
    *s1 += gu_le64(data[1]);  *s3 ^= *sB; *s0 ^= *s1; *s1 =GU_ROTL64(*s1,32); *s0 += *s2;
    *s2 += gu_le64(data[2]);  *s4 ^= *s0; *s1 ^= *s2; *s2 =GU_ROTL64(*s2,43); *s1 += *s3;
    *s3 += gu_le64(data[3]);  *s5 ^= *s1; *s2 ^= *s3; *s3 =GU_ROTL64(*s3,31); *s2 += *s4;
    *s4 += gu_le64(data[4]);  *s6 ^= *s2; *s3 ^= *s4; *s4 =GU_ROTL64(*s4,17); *s3 += *s5;
    *s5 += gu_le64(data[5]);  *s7 ^= *s3; *s4 ^= *s5; *s5 =GU_ROTL64(*s5,28); *s4 += *s6;
    *s6 += gu_le64(data[6]);  *s8 ^= *s4; *s5 ^= *s6; *s6 =GU_ROTL64(*s6,39); *s5 += *s7;
    *s7 += gu_le64(data[7]);  *s9 ^= *s5; *s6 ^= *s7; *s7 =GU_ROTL64(*s7,57); *s6 += *s8;
    *s8 += gu_le64(data[8]);  *sA ^= *s6; *s7 ^= *s8; *s8 =GU_ROTL64(*s8,55); *s7 += *s9;
    *s9 += gu_le64(data[9]);  *sB ^= *s7; *s8 ^= *s9; *s9 =GU_ROTL64(*s9,54); *s8 += *sA;
    *sA += gu_le64(data[10]); *s0 ^= *s8; *s9 ^= *sA; *sA =GU_ROTL64(*sA,22); *s9 += *sB;
    *sB += gu_le64(data[11]); *s1 ^= *s9; *sA ^= *sB; *sB =GU_ROTL64(*sB,46); *sA += *s0;
}

//
// Mix all 12 inputs together so that h0, h1 are a hash of them all.
//
// For two inputs differing in just the input bits
// Where "differ" means xor or subtraction
// And the base value is random, or a counting value starting at that bit
// The final result will have each bit of h0, h1 flip
// For every input bit,
// with probability 50 +- .3%
// For every pair of input bits,
// with probability 50 +- 3%
//
// This does not rely on the last Mix() call having already mixed some.
// Two iterations was almost good enough for a 64-bit result, but a
// 128-bit result is reported, so End() does three iterations.
//
static GU_FORCE_INLINE void _spooky_end_part(
    uint64_t* h0, uint64_t* h1, uint64_t* h2, uint64_t* h3,
    uint64_t* h4, uint64_t* h5, uint64_t* h6, uint64_t* h7,
    uint64_t* h8, uint64_t* h9, uint64_t* h10,uint64_t* h11)
{
    *h11+= *h1;    *h2 ^= *h11;   *h1 = GU_ROTL64(*h1,44);
    *h0 += *h2;    *h3 ^= *h0;    *h2 = GU_ROTL64(*h2,15);
    *h1 += *h3;    *h4 ^= *h1;    *h3 = GU_ROTL64(*h3,34);
    *h2 += *h4;    *h5 ^= *h2;    *h4 = GU_ROTL64(*h4,21);
    *h3 += *h5;    *h6 ^= *h3;    *h5 = GU_ROTL64(*h5,38);
    *h4 += *h6;    *h7 ^= *h4;    *h6 = GU_ROTL64(*h6,33);
    *h5 += *h7;    *h8 ^= *h5;    *h7 = GU_ROTL64(*h7,10);
    *h6 += *h8;    *h9 ^= *h6;    *h8 = GU_ROTL64(*h8,13);
    *h7 += *h9;    *h10^= *h7;    *h9 = GU_ROTL64(*h9,38);
    *h8 += *h10;   *h11^= *h8;    *h10= GU_ROTL64(*h10,53);
    *h9 += *h11;   *h0 ^= *h9;    *h11= GU_ROTL64(*h11,42);
    *h10+= *h0;    *h1 ^= *h10;   *h0 = GU_ROTL64(*h0,54);
}

static GU_FORCE_INLINE void _spooky_end(
    uint64_t* h0, uint64_t* h1, uint64_t* h2, uint64_t* h3,
    uint64_t* h4, uint64_t* h5, uint64_t* h6, uint64_t* h7,
    uint64_t* h8, uint64_t* h9, uint64_t* h10,uint64_t* h11)
{
#if 0
    _spooky_end_part(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
    _spooky_end_part(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
    _spooky_end_part(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
#endif
    int i;
    for (i = 0; i < 3; i++)
    {
        _spooky_end_part(h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11);
    }
}

//
// The goal is for each bit of the input to expand into 128 bits of
//   apparent entropy before it is fully overwritten.
// n trials both set and cleared at least m bits of h0 h1 h2 h3
//   n: 2   m: 29
//   n: 3   m: 46
//   n: 4   m: 57
//   n: 5   m: 107
//   n: 6   m: 146
//   n: 7   m: 152
// when run forwards or backwards
// for all 1-bit and 2-bit diffs
// with diffs defined by either xor or subtraction
// with a base of all zeros plus a counter, or plus another bit, or random
//
static GU_FORCE_INLINE void _spooky_short_mix(uint64_t* h0, uint64_t* h1,
                                              uint64_t* h2, uint64_t* h3)
{
    *h2 = GU_ROTL64(*h2,50);  *h2 += *h3;  *h0 ^= *h2;
    *h3 = GU_ROTL64(*h3,52);  *h3 += *h0;  *h1 ^= *h3;
    *h0 = GU_ROTL64(*h0,30);  *h0 += *h1;  *h2 ^= *h0;
    *h1 = GU_ROTL64(*h1,41);  *h1 += *h2;  *h3 ^= *h1;
    *h2 = GU_ROTL64(*h2,54);  *h2 += *h3;  *h0 ^= *h2;
    *h3 = GU_ROTL64(*h3,48);  *h3 += *h0;  *h1 ^= *h3;
    *h0 = GU_ROTL64(*h0,38);  *h0 += *h1;  *h2 ^= *h0;
    *h1 = GU_ROTL64(*h1,37);  *h1 += *h2;  *h3 ^= *h1;
    *h2 = GU_ROTL64(*h2,62);  *h2 += *h3;  *h0 ^= *h2;
    *h3 = GU_ROTL64(*h3,34);  *h3 += *h0;  *h1 ^= *h3;
    *h0 = GU_ROTL64(*h0,5);   *h0 += *h1;  *h2 ^= *h0;
    *h1 = GU_ROTL64(*h1,36);  *h1 += *h2;  *h3 ^= *h1;
}

//
// Mix all 4 inputs together so that h0, h1 are a hash of them all.
//
// For two inputs differing in just the input bits
// Where "differ" means xor or subtraction
// And the base value is random, or a counting value starting at that bit
// The final result will have each bit of h0, h1 flip
// For every input bit,
// with probability 50 +- .3% (it is probably better than that)
// For every pair of input bits,
// with probability 50 +- .75% (the worst case is approximately that)
//
static GU_FORCE_INLINE void _spooky_short_end(uint64_t* h0, uint64_t* h1,
                                              uint64_t* h2, uint64_t* h3)
{
    *h3 ^= *h2;  *h2 = GU_ROTL64(*h2,15);  *h3 += *h2;
    *h0 ^= *h3;  *h3 = GU_ROTL64(*h3,52);  *h0 += *h3;
    *h1 ^= *h0;  *h0 = GU_ROTL64(*h0,26);  *h1 += *h0;
    *h2 ^= *h1;  *h1 = GU_ROTL64(*h1,51);  *h2 += *h1;
    *h3 ^= *h2;  *h2 = GU_ROTL64(*h2,28);  *h3 += *h2;
    *h0 ^= *h3;  *h3 = GU_ROTL64(*h3,9);   *h0 += *h3;
    *h1 ^= *h0;  *h0 = GU_ROTL64(*h0,47);  *h1 += *h0;
    *h2 ^= *h1;  *h1 = GU_ROTL64(*h1,54);  *h2 += *h1;
    *h3 ^= *h2;  *h2 = GU_ROTL64(*h2,32);  *h3 += *h2;
    *h0 ^= *h3;  *h3 = GU_ROTL64(*h3,25);  *h0 += *h3;
    *h1 ^= *h0;  *h0 = GU_ROTL64(*h0,63);  *h1 += *h0;
}

/* 0x3/0x7 on 32/64-bit platforms respectively */
#define GU_SPOOKY_ALIGNMENT_MASK (GU_WORD_BYTES - 1)

//
// short hash ... it could be used on any message,
// but it's used by Spooky just for short messages ( <= _spooky_bufSize )
//
static GU_INLINE void gu_spooky_short_host(
    const void* const message,
    size_t      const length,
    uint64_t*   const hash)
{
    union
    {
        const uint8_t* p8;
        uint32_t*      p32;
        uint64_t*      p64;
        uintptr_t      i;
    } u;

    u.p8 = (const uint8_t *)message;

#ifndef GU_ALLOW_UNALIGNED_READS
    uint64_t buf[_spooky_numVars << 1];
    if (u.i & GU_SPOOKY_ALIGNMENT_MASK)
    {
        /* message unaligned, make aligned copy and point to it */
        memcpy(buf, message, length);
        u.p64 = buf;
    }
#endif /* GU_ALLOW_UNALIGNED_READS */

    size_t   remainder = length & 0x1F; /* length%32 */

    /* author version : */
    // uint64_t a = gu_le64(*hash[0]);
    // uint64_t b = gu_le64(*hash[1]);
    /* consistent seed version: */
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = _spooky_const;
    uint64_t d = _spooky_const;

    if (length > 15)
    {
        const uint64_t *end = u.p64 + ((length >> 5) << 2); /* (length/32)*4 */

        // handle all complete sets of 32 bytes
        for (; u.p64 < end; u.p64 += 4)
        {
            c += gu_le64(u.p64[0]);
            d += gu_le64(u.p64[1]);
            _spooky_short_mix(&a, &b, &c, &d);
            a += gu_le64(u.p64[2]);
            b += gu_le64(u.p64[3]);
        }

        //Handle the case of 16+ remaining bytes.
        if (remainder >= 16)
        {
            c += gu_le64(u.p64[0]);
            d += gu_le64(u.p64[1]);
            _spooky_short_mix(&a, &b, &c, &d);
            u.p64 += 2;
            remainder -= 16;
        }
    }

    // Handle the last 0..15 bytes, and its length
    d = ((uint64_t)length) << 56;
    switch (remainder)
    {
    case 15:
        d += ((uint64_t)u.p8[14]) << 48;
        // fall through
    case 14:
        d += ((uint64_t)u.p8[13]) << 40;
        // fall through
    case 13:
        d += ((uint64_t)u.p8[12]) << 32;
        // fall through
    case 12:
        d += gu_le32(u.p32[2]);
        c += gu_le64(u.p64[0]);
        break;
    case 11:
        d += ((uint64_t)u.p8[10]) << 16;
        // fall through
    case 10:
        d += ((uint64_t)u.p8[9]) << 8;
        // fall through
    case 9:
        d += (uint64_t)u.p8[8];
        // fall through
    case 8:
        c += gu_le64(u.p64[0]);
        break;
    case 7:
        c += ((uint64_t)u.p8[6]) << 48;
        // fall through
    case 6:
        c += ((uint64_t)u.p8[5]) << 40;
        // fall through
    case 5:
        c += ((uint64_t)u.p8[4]) << 32;
        // fall through
    case 4:
        c += gu_le32(u.p32[0]);
        break;
    case 3:
        c += ((uint64_t)u.p8[2]) << 16;
        // fall through
    case 2:
        c += ((uint64_t)u.p8[1]) << 8;
        // fall through
    case 1:
        c += (uint64_t)u.p8[0];
        break;
    case 0:
        c += _spooky_const;
        d += _spooky_const;
    }

    _spooky_short_end(&a, &b, &c, &d);

    // @note - in native-endian order!
    hash[0] = a;
    hash[1] = b;
}

static GU_FORCE_INLINE void gu_spooky_short(
    const void* const message,
    size_t      const length,
    uint64_t*   const hash)
{
    uint64_t* const u64 = (uint64_t*)hash;
    gu_spooky_short_host(message, length, u64);
    u64[0] = gu_le64(u64[0]);
    u64[1] = gu_le64(u64[1]);
}

// do the whole hash in one call
static GU_INLINE void gu_spooky_inline (
    const void* const message,
    size_t      const length,
    uint64_t*   const hash)
{
#ifdef GU_USE_SPOOKY_SHORT
    if (length < _spooky_bufSize)
    {
        gu_spooky_short_base (message, length, hash);
        return;
    }
#endif /* GU_USE_SPOOKY_SHORT */

    uint64_t  h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11;
    uint64_t  buf[_spooky_numVars];
    uint64_t* end;

    union
    {
        const uint8_t* p8;
        uint64_t*      p64;
        uintptr_t      i;
    } u;

    size_t remainder;

    /* this is how the author wants it: a possibility for different seeds
     h0=h3=h6=h9  = gu_le64(hash[0]);
     h1=h4=h7=h10 = gu_le64(hash[1]);
     * this is how we want it - constant seed */
    h0=h3=h6=h9  = 0;
    h1=h4=h7=h10 = 0;
    h2=h5=h8=h11 = _spooky_const;

    u.p8 = (const uint8_t*) message;
    end  = u.p64 + (length/_spooky_blockSize)*_spooky_numVars;

    // handle all whole _spooky_blockSize blocks of bytes
#ifndef GU_ALLOW_UNALIGNED_READS
    if ((u.i & GU_SPOOKY_ALIGNMENT_MASK) == 0)
    {
#endif /* GU_ALLOW_UNALIGNED_READS */
        while (u.p64 < end)
        {
            _spooky_mix(u.p64, &h0,&h1,&h2,&h3,&h4,&h5,&h6,&h7,&h8,&h9,&h10,&h11);
            u.p64 += _spooky_numVars;
        }
#ifndef GU_ALLOW_UNALIGNED_READS
    }
    else
    {
        while (u.p64 < end)
        {
            memcpy(buf, u.p64, _spooky_blockSize);
            _spooky_mix(buf, &h0,&h1,&h2,&h3,&h4,&h5,&h6,&h7,&h8,&h9,&h10,&h11);
            u.p64 += _spooky_numVars;
        }
    }
#endif /* GU_ALLOW_UNALIGNED_READS */

    // handle the last partial block of _spooky_blockSize bytes
    remainder = (length - ((const uint8_t*)end - (const uint8_t*)message));
    memcpy(buf, end, remainder);
    memset(((uint8_t*)buf) + remainder, 0, _spooky_blockSize - remainder);
    ((uint8_t*)buf)[_spooky_blockSize - 1] = remainder;
    _spooky_mix(buf, &h0,&h1,&h2,&h3,&h4,&h5,&h6,&h7,&h8,&h9,&h10,&h11);

    // do some final mixing
    _spooky_end(&h0,&h1,&h2,&h3,&h4,&h5,&h6,&h7,&h8,&h9,&h10,&h11);

    /*! @note: in native order */
    hash[0] = h0;
    hash[1] = h1;
}

/* As is apparent from the gu_spooky_inline(), Spooky hash is enormous.
 * Since it has advantage only on long messages, it makes sense to make it
 * a regular function to avoid code bloat.
 * WARNING: does not do final endian conversion! */
extern void
gu_spooky128_host (const void* const msg, size_t const len, uint64_t* res);

/* returns hash in the canonical byte order, as a byte array */
static GU_FORCE_INLINE void
gu_spooky128 (const void* const msg, size_t const len, void* const res)
{
    uint64_t* const r = (uint64_t*)res;
    gu_spooky128_host (msg, len, r);
    r[0] = gu_le64(r[0]);
    r[1] = gu_le64(r[1]);
}

/* returns hash as an integer, in host byte-order */
static GU_FORCE_INLINE uint64_t
gu_spooky64 (const void* const msg, size_t const len)
{
    uint64_t res[2];
    gu_spooky128_host (msg, len, res);
    return res[0];
}

/* returns hash as an integer, in host byte-order */
static GU_FORCE_INLINE uint32_t
gu_spooky32 (const void* const msg, size_t const len)
{
    uint64_t res[2];
    gu_spooky128_host (msg, len, res);
    return (uint32_t)res[0];
}

#ifdef __cplusplus
}
#endif

#endif /* _gu_spooky_h_ */
