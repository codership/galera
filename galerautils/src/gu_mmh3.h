// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file MurmurHash3 header
 *
 * This code is based on the refrence C++ MurMurHash3 implementation by its
 * author Austin Appleby, who released it to public domain.
 *
 * $Id$
 */

#ifndef _gu_mmh3_h_
#define _gu_mmh3_h_

#include "gu_byteswap.h"

#include <string.h> // for memset() and memcpy()

#ifdef __cplusplus
extern "C" {
#endif

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

static GU_FORCE_INLINE uint32_t _mmh3_fmix32 (uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

static GU_FORCE_INLINE uint64_t _mmh3_fmix64 (uint64_t k)
{
    k ^= k >> 33;
    k *= GU_ULONG_LONG(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= GU_ULONG_LONG(0xc4ceb9fe1a85ec53);
    k ^= k >> 33;

    return k;
}

//-----------------------------------------------------------------------------

static uint32_t const _mmh3_32_c1 = 0xcc9e2d51;
static uint32_t const _mmh3_32_c2 = 0x1b873593;

static GU_FORCE_INLINE void
_mmh3_block_32 (uint32_t k1, uint32_t* h1)
{
    k1 *= _mmh3_32_c1;
    k1 = GU_ROTL32(k1,15);
    k1 *= _mmh3_32_c2;

    *h1 ^= k1;
    *h1 = GU_ROTL32(*h1,13);
    *h1 *= 5;
    *h1 += 0xe6546b64;
}

static GU_FORCE_INLINE void
_mmh3_blocks_32 (const uint32_t* const blocks,size_t const nblocks,uint32_t* h1)
{
    //----------
    // body

    size_t i;
    for (i = 0; i < nblocks; i++)
    {
//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here
        _mmh3_block_32 (gu_le32(blocks[i]), h1);/* convert from little-endian */
    }
}

static GU_FORCE_INLINE uint32_t
_mmh3_tail_32 (const uint8_t* const tail, size_t const len, uint32_t h1)
{
    //----------
    // tail

#if 0 /* Reference implementation */
    uint32_t k1 = 0;

    switch(len & 3)
    {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];

        k1 *= _mmh3_32_c1; k1 = GU_ROTL32(k1,15); k1 *= _mmh3_32_c2; h1 ^= k1;
    };
#else /* Optimized implementation */
    size_t const shift = (len & 3) << 3;
    if (shift)
    {
        uint32_t k1 = gu_le32(((uint32_t*)tail)[0]) & (0x00ffffff>>(24-shift));
        k1 *= _mmh3_32_c1; k1 = GU_ROTL32(k1,15); k1 *= _mmh3_32_c2; h1 ^= k1;
    }
#endif /* Optimized implementation */

    //----------
    // finalization

    h1 ^= len;
    h1 = _mmh3_fmix32(h1);

    return h1;
}

static GU_FORCE_INLINE uint32_t
_mmh32_seed (const void* key, size_t const len, uint32_t seed)
{
    size_t const nblocks = len >> 2;
    const uint32_t* const blocks = (const uint32_t*)key;
    const uint8_t*  const tail   = (const uint8_t*)(blocks + nblocks);

    _mmh3_blocks_32 (blocks, nblocks, &seed);

    return _mmh3_tail_32 (tail, len, seed);
}

// same as FNV32 seed
static uint32_t const GU_MMH32_SEED = GU_ULONG(2166136261);

/*! A function to hash buffer in one go */
#define gu_mmh32(_buf, _len) \
    _mmh32_seed (_buf, _len, GU_MMH32_SEED);

/*
 * 128-bit MurmurHash3
 */
static uint64_t const _mmh3_128_c1 = GU_ULONG_LONG(0x87c37b91114253d5);
static uint64_t const _mmh3_128_c2 = GU_ULONG_LONG(0x4cf5ad432745937f);

static GU_FORCE_INLINE void
_mmh3_128_block (uint64_t k1, uint64_t k2, uint64_t* h1, uint64_t* h2)
{
    k1 *= _mmh3_128_c1; k1 = GU_ROTL64(k1,31); k1 *= _mmh3_128_c2; *h1 ^= k1;

    *h1 = GU_ROTL64(*h1,27); *h1 += *h2; *h1 *= 5; *h1 += 0x52dce729;

    k2 *= _mmh3_128_c2; k2 = GU_ROTL64(k2,33); k2 *= _mmh3_128_c1; *h2 ^= k2;

    *h2 = GU_ROTL64(*h2,31); *h2 += *h1; *h2 *= 5; *h2 += 0x38495ab5;
}

static GU_FORCE_INLINE void
_mmh3_128_blocks (const uint64_t* const blocks, size_t const nblocks,
                  uint64_t* h1, uint64_t* h2)
{
    //----------
    // body

    size_t i;
    for(i = 0; i < nblocks; i++)
    {
//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here
        uint64_t k1 = gu_le64(blocks[i]);
        i++;
        uint64_t k2 = gu_le64(blocks[i]);

        _mmh3_128_block (k1, k2, h1, h2);
    }
}

#if __GNUC__ == 4 && __GNUC_MINOR__ < 7
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif /* < GCC 4.7.0 */

static GU_FORCE_INLINE void
_mmh3_128_tail (const uint8_t* const tail, size_t const len,
                uint64_t h1, uint64_t h2, uint64_t* const out)
{
    //----------
    // tail

    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch(len & 15)
    {
    case 15: k2 ^= ((uint64_t)tail[14]) << 48;
             // fall through
    case 14: k2 ^= ((uint64_t)tail[13]) << 40;
             // fall through
    case 13: k2 ^= ((uint64_t)tail[12]) << 32;
             // fall through
    case 12: k2 ^= ((uint64_t)tail[11]) << 24;
             // fall through
    case 11: k2 ^= ((uint64_t)tail[10]) << 16;
             // fall through
    case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;
             // fall through
    case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
        k2 *= _mmh3_128_c2; k2 = GU_ROTL64(k2,33); k2 *= _mmh3_128_c1; h2 ^= k2;
        k1 = gu_le64(((uint64_t*)tail)[0]);
        k1 *= _mmh3_128_c1; k1 = GU_ROTL64(k1,31); k1 *= _mmh3_128_c2; h1 ^= k1;
        break;
    case  8: k1 ^= ((uint64_t)tail[ 7]) << 56;
             // fall through
    case  7: k1 ^= ((uint64_t)tail[ 6]) << 48;
             // fall through
    case  6: k1 ^= ((uint64_t)tail[ 5]) << 40;
             // fall through
    case  5: k1 ^= ((uint64_t)tail[ 4]) << 32;
             // fall through
    case  4: k1 ^= ((uint64_t)tail[ 3]) << 24;
             // fall through
    case  3: k1 ^= ((uint64_t)tail[ 2]) << 16;
             // fall through
    case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;
             // fall through
    case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
             k1 *= _mmh3_128_c1;
             k1 = GU_ROTL64(k1,31);
             k1 *= _mmh3_128_c2;
             h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len; h2 ^= len;

    h1 += h2;
    h2 += h1;

    h1 = _mmh3_fmix64(h1);
    h2 = _mmh3_fmix64(h2);

    h1 += h2;
    h2 += h1;

    out[0] = h1;
    out[1] = h2;
}

static GU_FORCE_INLINE void
_mmh3_128_seed (const void* const key, size_t const len,
                uint64_t s1, uint64_t s2, uint64_t* const out)
{
    size_t const nblocks = (len >> 4) << 1; /* using 64-bit half-blocks */
    const uint64_t* const blocks = (const uint64_t*)(key);
    const uint8_t*  const tail   = (const uint8_t*)(blocks + nblocks);

    _mmh3_128_blocks (blocks, nblocks, &s1, &s2);
    _mmh3_128_tail   (tail, len, s1, s2, out);
}

// same as FNV128 seed
static uint64_t const GU_MMH128_SEED1 = GU_ULONG_LONG(0x6C62272E07BB0142);
static uint64_t const GU_MMH128_SEED2 = GU_ULONG_LONG(0x62B821756295C58D);

/* returns hash in the canonical byte order, as a byte array */
static GU_FORCE_INLINE void
gu_mmh128 (const void* const msg, size_t const len, void* const out)
{
    _mmh3_128_seed (msg, len, GU_MMH128_SEED1, GU_MMH128_SEED2, (uint64_t*)out);
    uint64_t* const res = (uint64_t*)out;
    res[0] = gu_le64(res[0]);
    res[1] = gu_le64(res[1]);
}

/* returns hash as an integer, in host byte-order */
static GU_FORCE_INLINE uint64_t
gu_mmh128_64 (const void* const msg, size_t len)
{
    uint64_t res[2];
    _mmh3_128_seed (msg, len, GU_MMH128_SEED1, GU_MMH128_SEED2, res);
    return res[0];
}

/* returns hash as an integer, in host byte-order */
static GU_FORCE_INLINE uint32_t
gu_mmh128_32 (const void* const msg, size_t len)
{
    uint64_t res[2];
    _mmh3_128_seed (msg, len, GU_MMH128_SEED1, GU_MMH128_SEED2, res);
    return (uint32_t)res[0];
}

/*
 * Functions to hash message by parts
 * (only 128-bit version, 32-bit is not relevant any more)
 */

typedef struct gu_mmh128_ctx
{
    uint64_t hash[2];
    uint64_t tail[2];
    size_t   length;
} gu_mmh128_ctx_t;

/*! Initialize/reset MMH context with a particular seed.
 *  The seed is two 8-byte _integers_, obviously in HOST BYTE ORDER.
 *  Should not be used directly. */
static GU_INLINE void
_mmh128_init_seed (gu_mmh128_ctx_t* const mmh,
                   uint64_t         const s1,
                   uint64_t         const s2)
{
    memset (mmh, 0, sizeof(*mmh));
    mmh->hash[0] = s1;
    mmh->hash[1] = s2;
}

/*! Initialize MMH context with a default Galera seed. */
#define gu_mmh128_init(_mmh) \
    _mmh128_init_seed (_mmh, GU_MMH128_SEED1, GU_MMH128_SEED2);

/*! Apeend message part to hash context */
static GU_INLINE void
gu_mmh128_append (gu_mmh128_ctx_t* const mmh,
                  const void*      part,
                  size_t           len)
{
    size_t tail_len = mmh->length & 15;

    mmh->length += len;

    if (tail_len) /* there's something in the tail */// do we need this if()?
    {
        size_t const to_fill  = 16 - tail_len;
        void*  const tail_end = (uint8_t*)mmh->tail + tail_len;

        if (len >= to_fill) /* we can fill a full block */
        {
            memcpy (tail_end, part, to_fill);
            _mmh3_128_block (gu_le64(mmh->tail[0]), gu_le64(mmh->tail[1]),
                             &mmh->hash[0], &mmh->hash[1]);
            part = ((char*)part) + to_fill;
            len -= to_fill;
        }
        else
        {
            memcpy (tail_end, part, len);
            return;
        }
    }

    size_t const nblocks = (len >> 4) << 1; /* using 64-bit half-blocks */
    const uint64_t* const blocks = (const uint64_t*)(part);

    _mmh3_128_blocks (blocks, nblocks, &mmh->hash[0], &mmh->hash[1]);

    /* save possible trailing bytes to tail */
    memcpy (mmh->tail, blocks + nblocks, len & 15);
}

/*! Get the accumulated message hash (does not change the context) */
static GU_INLINE void
gu_mmh128_get (const gu_mmh128_ctx_t* const mmh, void* const res)
{
    uint64_t r[2];
    _mmh3_128_tail ((const uint8_t*)mmh->tail, mmh->length,
                    mmh->hash[0], mmh->hash[1], r);
    r[0] = gu_le64(r[0]);
    r[1] = gu_le64(r[1]);
    memcpy(res, r, sizeof(r));
}

static GU_INLINE uint64_t
gu_mmh128_get64 (const gu_mmh128_ctx_t* const mmh)
{
    uint64_t res[2];
    _mmh3_128_tail ((const uint8_t*)mmh->tail, mmh->length,
                    mmh->hash[0], mmh->hash[1], res);
    return res[0];
}

static GU_INLINE uint32_t
gu_mmh128_get32 (const gu_mmh128_ctx_t* const mmh)
{
    uint64_t res[2];
    _mmh3_128_tail ((const uint8_t*)mmh->tail, mmh->length,
                    mmh->hash[0], mmh->hash[1], res);
    return (uint32_t)res[0];
}

/*
 * Below are fuctions with reference signatures for implementation verification
 */
extern void
gu_mmh3_32      (const void* key, int len, uint32_t seed, void* out);

#if 0 /* x86 variant is faulty and unsuitable for short keys, ignore */
extern void
gu_mmh3_x86_128 (const void* key, int len, uint32_t seed, void* out);
#endif /* 0 */

extern void
gu_mmh3_x64_128 (const void* key, int len, uint32_t seed, void* out);

#ifdef __cplusplus
}
#endif

#endif /* _gu_mmh3_h_ */
