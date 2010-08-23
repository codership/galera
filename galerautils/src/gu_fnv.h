/* Copyright (C) 2009 Codership Oy <info@codership.com> */
// THIS IS UNFINISHED!!!
#include <stdint.h>

#define GU_FNV_64_INIT  0xcbf29ce484222325ULL
#define GU_FNV_64_PRIME 0x100000001b3ULL
#define GU_FNV_64_PRIME_MULTIPLY_SHIFT(arg)             \
    arg += (arg << 1) + (arg << 4) + (arg << 5) +       \
        (arg << 7) + (arg << 8) + (arg << 40);


static inline uint64_t
gu_fnv1a (const void* buf, size_t buf_len)
{
    const uint8_t* bp = buf;
    const uint8_t* be = bp + buf_len;
    uint64_t ret = GU_FNV_64_INIT;

    while (bp < be) {
        ret ^= (uint64_t)*bp++;
        ret *= GU_FNV_64_PRIME;
    }

    return ret;
}

static inline uint64_t
gu_fnv1a_opt (const void* buf, size_t buf_len)
{
    const uint8_t* bp = buf;
    const uint8_t* be = bp + buf_len;
    uint64_t ret = GU_FNV_64_INIT;

    while (bp < be) {
        ret ^= (uint64_t)*bp++;
        GU_FNV_64_PRIME_MULTIPLY_SHIFT(ret);
    }

    return ret;
}

static inline uint64_t
gu_fnv1a_2 (const void* buf, size_t buf_len)
{
    const uint16_t* bp = buf;
    const uint16_t* be = bp + (buf_len >> 1);
    uint64_t ret = GU_FNV_64_INIT;

    while (bp < be) {
        ret ^= (uint64_t)*bp++;
        ret *= GU_FNV_64_PRIME;
    }

    if (buf_len & 1) { // odd length
        ret ^= (uint64_t)*(uint8_t*)be;
        ret *= GU_FNV_64_PRIME;
    }

    return ret;
}

static inline uint64_t
gu_fnv1a_2_opt (const void* buf, size_t buf_len)
{
    const uint16_t* bp = buf;
    const uint16_t* be = bp + (buf_len >> 1);
    uint64_t ret = GU_FNV_64_INIT;

    while (bp < be) {
        ret ^= (uint64_t)*bp++;
        GU_FNV_64_PRIME_MULTIPLY_SHIFT(ret);
    }

    if (buf_len & 1) { // odd length
        ret ^= (uint64_t)*(uint8_t*)be;
        GU_FNV_64_PRIME_MULTIPLY_SHIFT(ret);
    }

    return ret;
}

