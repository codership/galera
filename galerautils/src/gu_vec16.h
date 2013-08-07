// Copyright (C) 2013 Codership Oy <info@codership.com>

/**
 * @file 16-byte "vector" type and operations - mostly for checksums/hashes
 *
 * $Id$
 */

#ifndef _gu_vec16_h_
#define _gu_vec16_h_

#include "gu_macros.h"
#include "gu_byteswap.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>   /* bool */

#ifdef __cplusplus
extern "C" {
#endif

/* this type will generate SIMD instructions where possible: good for XORing */
typedef unsigned long gu_vec16__ __attribute__ ((vector_size (16)));

typedef union gu_vec16
{
    gu_vec16__ vec_;
    uint64_t   int_[2]; /* for equality we better use scalar type
                         * since we need scalar return */
} gu_vec16_t;

static GU_FORCE_INLINE gu_vec16_t
gu_vec16_from_byte (unsigned char b)
{
    gu_vec16_t ret;
    memset (&ret, b, sizeof(ret));
    return ret;
}

static GU_FORCE_INLINE gu_vec16_t
gu_vec16_from_ptr (const void* ptr)
{
    gu_vec16_t ret;
    memcpy (&ret, ptr, sizeof(ret));
    return ret;
}

static GU_FORCE_INLINE gu_vec16_t
gu_vec16_xor (gu_vec16_t l, gu_vec16_t r)
{
    gu_vec16_t ret;
    ret.vec_ = (l.vec_ ^ r.vec_);
    return ret;
}

static GU_FORCE_INLINE bool
gu_vec16_neq (gu_vec16_t l, gu_vec16_t r)
{
    return (l.int_[0] != r.int_[0] || l.int_[1] != r.int_[1]);
}

static GU_FORCE_INLINE bool
gu_vec16_eq (gu_vec16_t l, gu_vec16_t r)
{
    return !(gu_vec16_neq (l, r));
}

static GU_FORCE_INLINE gu_vec16_t
gu_vec16_bswap (gu_vec16_t x)
{
    gu_vec16_t ret;
    ret.int_[0] = gu_bswap64 (x.int_[1]);
    ret.int_[1] = gu_bswap64 (x.int_[0]);
    return ret;
}

#ifdef __cplusplus
}

static GU_FORCE_INLINE gu_vec16_t
operator^ (const gu_vec16_t& l, const gu_vec16_t& r)
{
    return (gu_vec16_xor (l, r));
}

static GU_FORCE_INLINE bool
operator== (const gu_vec16_t& l, const gu_vec16_t& r)
{
    return (gu_vec16_eq (l, r));
}

#endif

#endif /* _gu_vec16_h_ */
