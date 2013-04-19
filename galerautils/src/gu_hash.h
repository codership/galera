// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file Defines 3 families of standard Galera hash methods
 *
 * 1) gu_hash       - a general use universal hash: 128, 64 and 32-bit variants.
 *
 * 2) gu_fast_hash  - optimized for 64-bit Intel CPUs, limited to whole message
 *                    only, also comes in 128, 64 and 32-bit flavors.
 * 3) gu_table_hash - possibly even faster, platform-optimized, globally
 *                    inconsistent hash functions to be used only in local hash
 *                    tables. Only size_t variants defined.
 *
 * 128-bit result is returned through void* parameter as a byte array in
 * canonical order.
 * 64/32-bit results are returned as uint64_t/uint32_t integers and thus in host
 * byte order (require conversion to network/Galera byte order for serialization).
 *
 * $Id$
 */

#ifndef _gu_hash_h_
#define _gu_hash_h_

#ifdef __cplusplus
extern "C" {
#endif

#include "gu_fnv.h"
#include "gu_mmh3.h"
#include "gu_spooky.h"

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

#define GU_SHORT64_LIMIT  16
#define GU_MEDIUM64_LIMIT 512

static GU_INLINE void
gu_fast_hash128 (const void* const msg, size_t const len, void* const res)
{
    if (len < GU_MEDIUM64_LIMIT)
    {
        gu_mmh128 (msg, len, res);
    }
    else
    {
        gu_spooky128 (msg, len, res);
    }
}

static GU_FORCE_INLINE uint64_t
gu_fast_hash64_short (const void* const msg, size_t const len)
{
    uint64_t res = GU_FNV64_SEED;
    gu_fnv64a_internal (msg, len, &res);
    /* mix to improve avalanche effect */
    res *= GU_ROTL64(res, 56);
    return res ^ GU_ROTL64(res, 43);
}

#define gu_fast_hash64_medium  gu_mmh128_64
#define gu_fast_hash64_long    gu_spooky64

static GU_INLINE uint64_t
gu_fast_hash64 (const void* const msg, size_t const len)
{
    if (len < GU_SHORT64_LIMIT)
    {
        return gu_fast_hash64_short (msg, len);
    }
    else if (len < GU_MEDIUM64_LIMIT)
    {
        return gu_fast_hash64_medium (msg, len);
    }
    else
    {
        return gu_fast_hash64_long (msg, len);
    }
}

#define gu_fast_hash32_short   gu_mmh32
#define gu_fast_hash32_medium  gu_mmh128_32
#define gu_fast_hash32_long    gu_spooky32

#define GU_SHORT32_LIMIT  32
#define GU_MEDIUM32_LIMIT 512

static GU_INLINE uint32_t
gu_fast_hash32 (const void* const msg, size_t const len)
{
    if (len < GU_SHORT32_LIMIT)
    {
        return gu_fast_hash32_short (msg, len);
    }
    else if (len < GU_MEDIUM32_LIMIT)
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

/* on 32-bit platform MurmurHash32 is only insignificantly slower than FNV32a
 * on messages < 10 bytes but produces far better hash. */
#define gu_table_hash   gu_mmh32       /* size_t is normally 32-bit here */

#else /* GU_WORDSIZE neither 64 nor 32 bits */
#  error Unsupported wordsize!
#endif

#ifdef __cplusplus
}
#endif

#endif /* _gu_hash_h_ */

