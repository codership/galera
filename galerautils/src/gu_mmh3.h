// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file MurmurHash3 header
 *
 * $Id$
 */

#ifndef _gu_mmh3_h_
#define _gu_mmh3_h_

#include "gu_byteswap.h"

#ifdef __cplusplus
extern "C" {
#endif

#if 0
//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

static GU_FORCE_INLINE GU_UNUSED uint32_t _mmh3_getblock32 (const uint32_t* p, int i)
{
    return gu_le32(p[i]);
}

static GU_FORCE_INLINE GU_UNUSED uint64_t _mmh3_getblock64 (const uint64_t* p, int i)
{
    return gu_le64(p[i]);
}
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

static GU_FORCE_INLINE GU_UNUSED uint64_t _mmh3_fmix64 (uint64_t k)
{
    k ^= k >> 33;
    k *= GU_BIG_CONSTANT(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= GU_BIG_CONSTANT(0xc4ceb9fe1a85ec53);
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
        _mmh3_block_32 (gu_le32(blocks[i]), h1);/* convert from little-endian */
    }
}

static GU_FORCE_INLINE uint32_t
_mmh3_tail_32 (const uint8_t* const tail, size_t const len, uint32_t h1)
{
    //----------
    // tail

    uint32_t k1 = 0;

    switch(len & 3)
    {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];

        k1 *= _mmh3_32_c1; k1 = GU_ROTL32(k1,15); k1 *= _mmh3_32_c2; h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len;
    h1 = _mmh3_fmix32(h1);

    return gu_le32(h1);  /* convert to little-endian */
}

static GU_INLINE void
_mmh32_seed (const void* key, int const len, uint32_t seed, void* out)
{
    size_t const nblocks = len >> 2;
    const uint32_t* const blocks = (const uint32_t*)key;
    const uint8_t*  const tail   = (const uint8_t*)(blocks + nblocks);

    _mmh3_blocks_32 (blocks, nblocks, &seed);

    *(uint32_t*)out = _mmh3_tail_32 (tail, len, seed);
}

static uint32_t const GU_MMH32_SEED = 2166136261; // same as FNV32 seed

/*! A function to hash buffer in one go */
static GU_INLINE void
gu_mmh32 (const void* buf, int const buf_len, uint32_t* out)
{
    _mmh32_seed (buf, buf_len, GU_MMH32_SEED, out);
}

/*
 * 128-bit MurmurHash3
 */
static uint64_t const _mmh3_128_c1 = GU_BIG_CONSTANT(0x87c37b91114253d5);
static uint64_t const _mmh3_128_c2 = GU_BIG_CONSTANT(0x4cf5ad432745937f);

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
    for(i = 0; i < nblocks;)
    {
        uint64_t k1 = gu_le64(blocks[i++]);
        uint64_t k2 = gu_le64(blocks[i++]);

        _mmh3_128_block (k1, k2, h1, h2);
    }
}

static GU_FORCE_INLINE void
_mmh3_128_tail (const uint8_t* const tail, size_t const len,
                uint64_t h1, uint64_t h2, void* out)
{
    //----------
    // tail

    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch(len & 15)
    {
    case 15: k2 ^= ((uint64_t)tail[14]) << 48;
    case 14: k2 ^= ((uint64_t)tail[13]) << 40;
    case 13: k2 ^= ((uint64_t)tail[12]) << 32;
    case 12: k2 ^= ((uint64_t)tail[11]) << 24;
    case 11: k2 ^= ((uint64_t)tail[10]) << 16;
    case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;
    case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
        k2 *= _mmh3_128_c2; k2 = GU_ROTL64(k2,33); k2 *= _mmh3_128_c1; h2 ^= k2;

    case  8: k1 ^= ((uint64_t)tail[ 7]) << 56;
    case  7: k1 ^= ((uint64_t)tail[ 6]) << 48;
    case  6: k1 ^= ((uint64_t)tail[ 5]) << 40;
    case  5: k1 ^= ((uint64_t)tail[ 4]) << 32;
    case  4: k1 ^= ((uint64_t)tail[ 3]) << 24;
    case  3: k1 ^= ((uint64_t)tail[ 2]) << 16;
    case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;
    case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
        k1 *= _mmh3_128_c1; k1 = GU_ROTL64(k1,31); k1 *= _mmh3_128_c2; h1 ^= k1;
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

    ((uint64_t*)out)[0] = gu_le64(h1);
    ((uint64_t*)out)[1] = gu_le64(h2);
}

static GU_INLINE void
_mmh3_128_seed (const void* const key, size_t const len,
                uint64_t s1, uint64_t s2, void* const out)
{
    size_t const nblocks = (len >> 4) << 1; /* using 64-bit half-blocks */
    const uint64_t* const blocks = (const uint64_t *)(key);
    const uint8_t*  const tail   = (const uint8_t*)(blocks + nblocks);

    _mmh3_128_blocks (blocks, nblocks, &s1, &s2);
    _mmh3_128_tail   (tail, len, s1, s2, out);
}

// same as FNV128 seed
static uint64_t const GU_MMH128_SEED1 = GU_BIG_CONSTANT(0x6C62272E07BB0142);
static uint64_t const GU_MMH128_SEED2 = GU_BIG_CONSTANT(0x62B821756295C58D);

static GU_INLINE void
gu_mmh128 (const void* key, int len, void* out)
{
    _mmh3_128_seed (key, len, GU_MMH128_SEED1, GU_MMH128_SEED2, out);
}

/*
 * Below are fuctions with reference signatures for implementation verification
 */
extern void
gu_mmh3_32      (const void* key, int len, uint32_t seed, void* out);

extern void
gu_mmh3_x86_128 (const void* key, int len, uint32_t seed, void* out);

extern void
gu_mmh3_x64_128 (const void* key, int len, uint32_t seed, void* out);

#ifdef __cplusplus
}
#endif

#if (GU_WORDSIZE == 32)
#  define gu_mmh3_128 gu_mmh3_x86_128
#elif (GU_WORDSIZE == 64)
#  define gu_mmh3_128 gu_mmh3_x64_128
#else
#  error "Unsupported wordsize"
#endif

#endif /* _gu_mmh3_h_ */
