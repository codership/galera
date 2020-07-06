// Copyright (C) 2012-2020 Codership Oy <info@codership.com>

/**
 * @file MurmurHash3 header
 *
 * This code is based on the reference C++ MurMurHash3 implementation by its
 * author Austin Appleby, who released it to public domain.
 *
 * $Id$
 */

#ifndef _gu_mmh3_h_
#define _gu_mmh3_h_

#include <stdint.h> // uint*_t
#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

/*! A function to hash buffer in one go */
extern uint32_t
gu_mmh32(const void* buf, size_t len);

/*
 * 128-bit MurmurHash3
 */

/* returns hash in the canonical byte order, as a byte array */
extern void
gu_mmh128 (const void* const msg, size_t const len, void* const out);

/* returns hash as an integer, in host byte-order */
extern uint64_t
gu_mmh128_64 (const void* const msg, size_t len);

/* returns hash as an integer, in host byte-order */
extern uint32_t
gu_mmh128_32 (const void* const msg, size_t len);

/*
 * Functions to hash stream
 * (only 128-bit version, 32-bit is not relevant any more)
 */

typedef struct gu_mmh128_ctx
{
    uint64_t hash[2];
    uint64_t tail[2];
    size_t   length;
} gu_mmh128_ctx_t;

/*! Initialize MMH context with a default Galera seed. */
extern void
gu_mmh128_init  (gu_mmh128_ctx_t* mmh);

/*! Apeend message part to hash context */
extern void
gu_mmh128_append(gu_mmh128_ctx_t* mmh, const void* part, size_t len);

/*! Get the accumulated message hash (does not change the context) */
extern void
gu_mmh128_get  (const gu_mmh128_ctx_t* mmh, void* const res);

extern uint64_t
gu_mmh128_get64(const gu_mmh128_ctx_t* mmh);

extern uint32_t
gu_mmh128_get32(const gu_mmh128_ctx_t* mmh);

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
