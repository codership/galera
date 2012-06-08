// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file Defines 3 families of standard Galera hash methods
 *
 * 1) gu_hash       - a general use universal hash,
 *                    comes in 128, 64 and 32-bit flavors.
 * 2) gu_fast_hash  - possibly faster hash methods, limited to whole message
 *                    only, also come in 128, 64 and 32-bit flavors.
 * 3) gu_table_hash - possibly even faster, platform-optimized, globally
 *                    inconsistent hash functions to be used only in local hash
 *                    tables. Only 32 and 64-bit variants defined.
 *
 * 128-bit result is returned through void* parameter, 64/32-bit results are
 * returned as uint64_t/uint32_t return value.
 *
 * $Id$
 */

#ifndef _gu_hash_h_
#define _gu_hash_h_

#include "gu_fnv.h"
#include "gu_mmh3.h"
#include "gu_spooky.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * General purpose globally consistent _fast_ hash, if in doubt use that.
 */

/* This is to hash multipart message */
#define gu_hash_t                         gu_mmh128_ctx_t
#define gu_hash_init(_hash)               gu_mmh128_init(_hash)
#define gu_hash_append(_hash, _msg, _len) gu_mmh128_append(_hash, _msg, _len)
#define gu_hash_get128(_hash, _res)       gu_mmh128_get(_hash, _res)
#define gu_hash_get64(_hash)              gu_mmh128_get64(_hash)
#define gu_hash_get32(_hash)              gu_mmh128_get32(_hash)

/* This is to hash a whole message in one go */
#define gu_hash128(_msg, _len, _res)      gu_mmh128(_msg, _len, _res)
#define gu_hash64(_msg, _len)             gu_mmh128_64(_msg, _len)
#define gu_hash32(_msg, _len)             gu_mmh128_32(_msg, _len)

/*
 * Hash optimized for speed, can't do multipart messages, but should still
 * be usable as global identifier
 */

#define GU_SHORT_LIMIT 16
#define GU_MEDIUM_LIMIT 1025

static GU_INLINE void
gu_fast_hash128 (const void* const msg, size_t const len, void* const res)
{
    if (len < GU_MEDIUM_LIMIT)
    {
        gu_mmh128 (msg, len, res);
    }
    else
    {
        gu_spooky128 (msg, len, res);
    }
}

static GU_INLINE uint64_t
gu_fast_hash64_short (const void* const msg, size_t const len)
{
    uint64_t res = GU_FNV64_SEED;
    gu_fnv64a_internal (msg, len, &res);
    /* to make 8th bit variations to escalate to 1st */
    res ^= GU_ROTL64(res, 25);
    return gu_le64(res);
}

#define gu_fast_hash64_medium(_msg, _len)  gu_mmh128_64(_msg, _len)
#define gu_fast_hash64_long(_msg, _len)    gu_spooky64(_msg, _len)

static GU_INLINE uint64_t
gu_fast_hash64 (const void* const msg, size_t const len)
{
    if (len < GU_SHORT_LIMIT)
    {
        return gu_fast_hash64_short (msg, len);
    }
    else if (len < GU_MEDIUM_LIMIT)
    {
        return gu_fast_hash64_medium (msg, len);
    }
    else
    {
        return gu_fast_hash64_long (msg, len);
    }
}

static GU_INLINE uint32_t
gu_fast_hash32_short (const void* const msg, size_t const len)
{
    uint32_t res = GU_FNV32_SEED;
    gu_fnv32a_internal (msg, len, &res);
    /* to make 8th bit variations to escalate to 1st */
    res ^= GU_ROTL32(res, 17);
    return gu_le32(res);
}

#define gu_fast_hash32_medium(_msg, _len)  gu_mmh128_32(_msg, _len)
#define gu_fast_hash32_long(_msg, _len)    gu_spooky32(_msg, _len)

static GU_INLINE uint64_t
gu_fast_hash32 (const void* const msg, size_t const len)
{
    if (len < GU_SHORT_LIMIT)
    {
        return gu_fast_hash32_short (msg, len);
    }
    else if (len < GU_MEDIUM_LIMIT)
    {
        return gu_fast_hash32_medium (msg, len);
    }
    else
    {
        return gu_fast_hash32_long (msg, len);
    }
}

/*
 * Platform-optimized hashes only for local hash tables, don't produce globally
 * consistent results. No 128-bit version for obvious reasons.
 *
 * Resulting gu_table_hash() will be the fastest hash function returning size_t
 */

#if GU_WORDSIZE == 64

#define gu_table_hash   gu_fast_hash64 /* size_t is normally 64-bit here */

#elif GU_WORDSIZE == 32

static GU_INLINE uint32_t
gu_table_hash32_short (const void* const msg, size_t const len)
{
    uint32_t res = GU_FNV32_SEED;
    gu_fnv32a_internal (msg, len, &res);
    /* to make 8th bit variations to escalate to 1st */
    res ^= GU_ROTL32(res, 17);
    return res;
}

#define gu_table_hash32_long(_msg, _len)  gu_mmh32(_msg, _len)

static GU_INLINE uint64_t
gu_table_hash (const void* const msg, size_t const len)
{
    if (len < 16)
    {
        return gu_table_hash32_short (msg, len);
    }
    else
    {
        return gu_table_hash32_long (msg, len);
    }
}

#else /* GU_WORDSIZE neither 64 nor 32 bits */
#  error Unsupported wordsize!
#endif

#ifdef __cplusplus
}
#endif

#endif /* _gu_hash_h_ */

