// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file MurmurHash3 implementation
 *       (slightly rewritten from the refrence C++ impl.)
 *
 * $Id$
 */

#include "gu_mmh3.h"

void
gu_mmh3_32 (const void* const key, int const len, uint32_t const seed, void* const out)
{
    uint32_t const res = _mmh32_seed (key, len, seed);
    *((uint32_t*)out)  = gu_le32(res);
}

//-----------------------------------------------------------------------------

#if 0 /* x86 variant is faulty and unsuitable for short keys, ignore */
void gu_mmh3_x86_128 (const void* key, const int len,
                      const uint32_t seed, void* out)
{
    const uint8_t* data = (const uint8_t*)key;
    const int nblocks = len >> 4;

    uint32_t h1 = seed;
    uint32_t h2 = seed;
    uint32_t h3 = seed;
    uint32_t h4 = seed;

    const uint32_t c1 = 0x239b961b;
    const uint32_t c2 = 0xab0e9789;
    const uint32_t c3 = 0x38b34ae5;
    const uint32_t c4 = 0xa1e38b93;

    //----------
    // body

    const uint32_t* blocks = (const uint32_t*)(data + (nblocks << 4));

    int i;
    for(i = -nblocks; i; i++)
    {
        uint32_t k1 = gu_le32(blocks[(i << 2) + 0]);
        uint32_t k2 = gu_le32(blocks[(i << 2) + 1]);
        uint32_t k3 = gu_le32(blocks[(i << 2) + 2]);
        uint32_t k4 = gu_le32(blocks[(i << 2) + 3]);

        k1 *= c1; k1 = GU_ROTL32(k1,15); k1 *= c2; h1 ^= k1;

        h1 = GU_ROTL32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;

        k2 *= c2; k2 = GU_ROTL32(k2,16); k2 *= c3; h2 ^= k2;

        h2 = GU_ROTL32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;

        k3 *= c3; k3 = GU_ROTL32(k3,17); k3 *= c4; h3 ^= k3;

        h3 = GU_ROTL32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;

        k4 *= c4; k4 = GU_ROTL32(k4,18); k4 *= c1; h4 ^= k4;

        h4 = GU_ROTL32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;
    }

    //----------
    // tail

    const uint8_t * tail = (const uint8_t*)(blocks);

    uint32_t k1 = 0;
    uint32_t k2 = 0;
    uint32_t k3 = 0;
    uint32_t k4 = 0;

    switch(len & 15)
    {
    case 15: k4 ^= tail[14] << 16;
    case 14: k4 ^= tail[13] << 8;
    case 13: k4 ^= tail[12] << 0;
        k4 *= c4; k4  = GU_ROTL32(k4,18); k4 *= c1; h4 ^= k4;

    case 12: k3 ^= tail[11] << 24;
    case 11: k3 ^= tail[10] << 16;
    case 10: k3 ^= tail[ 9] << 8;
    case  9: k3 ^= tail[ 8] << 0;
        k3 *= c3; k3  = GU_ROTL32(k3,17); k3 *= c4; h3 ^= k3;

    case  8: k2 ^= tail[ 7] << 24;
    case  7: k2 ^= tail[ 6] << 16;
    case  6: k2 ^= tail[ 5] << 8;
    case  5: k2 ^= tail[ 4] << 0;
        k2 *= c2; k2  = GU_ROTL32(k2,16); k2 *= c3; h2 ^= k2;

    case  4: k1 ^= tail[ 3] << 24;
    case  3: k1 ^= tail[ 2] << 16;
    case  2: k1 ^= tail[ 1] << 8;
    case  1: k1 ^= tail[ 0] << 0;
        k1 *= c1; k1  = GU_ROTL32(k1,15); k1 *= c2; h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    h1 = _mmh3_fmix32(h1);
    h2 = _mmh3_fmix32(h2);
    h3 = _mmh3_fmix32(h3);
    h4 = _mmh3_fmix32(h4);

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    ((uint32_t*)out)[0] = gu_le32(h1);
    ((uint32_t*)out)[1] = gu_le32(h2);
    ((uint32_t*)out)[2] = gu_le32(h3);
    ((uint32_t*)out)[3] = gu_le32(h4);
}
#endif /* 0 */
//-----------------------------------------------------------------------------

void
gu_mmh3_x64_128 (const void* key, int len, uint32_t const seed, void* const out)
{
    uint64_t* const res = (uint64_t*)out;
    _mmh3_128_seed (key, len, seed, seed, res);
    res[0] = gu_le64(res[0]);
    res[1] = gu_le64(res[1]);
}


