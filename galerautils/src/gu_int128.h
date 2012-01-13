// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file 128-bit arithmetic macros. This is so far needed only for FNV128
 *       hash algorithm
 *
 * $Id$
 */

#ifndef _gu_arch_h_
#define _gu_arch_h_

#include "gu_arch.h"

#include <inttypes.h>

#if (GU_WORDSIZE == 64)

typedef          int __attribute__((__mode__(__TI__)))  int128_t;
typedef unsigned int __attribute__((__mode__(__TI__))) uint128_t;

typedef int128_t  gu_int128_t;
typedef uint128_t gu_uint128_t;

#define GU_SET128(_a, hi64, lo64) _a = (typeof(_a))hi64 << 64 + lo64;
#define GU_MUL128_INPLACE(_a, _b) _a *= _b
#define GU_IMUL128_INPLACE(_a, _b) GU_MUL128_INPLACE(_a, _b)
#define GU_EQ128(_a, _b) (_a == _b)

#else /* GU_WORDSIZE == 32 */

#if (GU_BYTE_ORDER == GU_LITTLE_ENDIAN)

#define GU_64HI 1
#define GU_64LO 0
#define GU_32HI 3
#define GU_32LO 0

typedef union gu_int128 {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {uint32_t lo; uint64_t mid; int32_t hi;}__attribute__((packed)) m;
#ifdef __cplusplus
    gu_int128() {}
    gu_int128(int64_t hi, uint64_t lo) { u64[0] = lo; u64[1] = hi; }
#endif
} gu_int128_t; 

typedef union gu_uint128 {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {uint32_t lo; uint64_t mid; uint32_t hi;}__attribute__((packed)) m;
#ifdef __cplusplus
    gu_uint128() {}
    gu_uint128(uint64_t hi, uint64_t lo) { u64[0] = lo; u64[1] = hi; }
#endif
} gu_uint128_t; 

#ifdef __cplusplus
#define GU_SET128(_a, hi64, lo64) _a = gu_uint128(hi64, lo64);
#else
#define GU_SET128(_a, hi64, lo64) _a = { .u64 = { lo64, hi64 } }
#endif

#define GU_MUL128_INPLACE(_a,_b) {                       \
    uint64_t m00 = (uint64_t)(_a).u32[0] * (_b).u32[0];  \
    uint64_t m10 = (uint64_t)(_a).u32[1] * (_b).u32[0];  \
    uint64_t m20 = (uint64_t)(_a).u32[2] * (_b).u32[0];  \
    uint64_t m01 = (uint64_t)(_a).u32[0] * (_b).u32[1];  \
    uint64_t m02 = (uint64_t)(_a).u32[0] * (_b).u32[2];  \
    uint64_t m11 = (uint64_t)(_a).u32[1] * (_b).u32[1];  \
    uint32_t m30 = (_a).u32[3] * (_b).u32[0];            \
    uint32_t m21 = (_a).u32[2] * (_b).u32[1];            \
    uint32_t m12 = (_a).u32[1] * (_b).u32[2];            \
    uint32_t m03 = (_a).u32[0] * (_b).u32[3];            \
    (_a).u64[GU_64LO]  = m00; (_a).u64[GU_64HI] = 0;     \
    (_a).m.mid  += m10; (_a).m.hi += ((_a).m.mid < m10); \
    (_a).m.mid  += m01; (_a).m.hi += ((_a).m.mid < m01); \
    (_a).u64[GU_64HI] += m20 + m11 + m02;                \
    (_a).u32[GU_32HI] += m30 + m21 + m12 + m03;          \
}

#else /* GU_BIG_ENDIAN */

#define GU_64HI 0
#define GU_64LO 1
#define GU_32HI 0
#define GU_32LO 3

typedef union {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {int32_t hi; uint64_t mid; uint32_t lo;}__attribute__((packed)) m;
} gu_int128_t; 

typedef union {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {uint32_t hi; uint64_t mid; uint32_t lo;}__attribute__((packed)) m;
} gu_uint128_t; 

#define GU_SET128(_a, hi64, lo64) _a = { .u64 = { hi64, lo64 } }

#define GU_MUL128_INPLACE(_a,_b) {                   \
    uint64_t m33 = (uint64_t)_a.u32[3] * _b.u32[3];  \
    uint64_t m23 = (uint64_t)_a.u32[2] * _b.u32[3];  \
    uint64_t m13 = (uint64_t)_a.u32[1] * _b.u32[3];  \
    uint64_t m32 = (uint64_t)_a.u32[3] * _b.u32[2];  \
    uint64_t m31 = (uint64_t)_a.u32[3] * _b.u32[1];  \
    uint64_t m22 = (uint64_t)_a.u32[2] * _b.u32[2];  \
    uint32_t m30 = _a.u32[3] * _b.u32[0];  \
    uint32_t m21 = _a.u32[2] * _b.u32[1];  \
    uint32_t m12 = _a.u32[1] * _b.u32[2];  \
    uint32_t m03 = _a.u32[0] * _b.u32[3];  \
    _a.u64[GU_64LO]  = m00; _a.u64[GU_64HI] = 0;     \
    _a.m.mid  += m23; _a.m.hi += (_a.m.mid < m23); \
    _a.m.mid  += m32; _a.m.hi += (_a.m.mid < m32); \
    _a.u64[GU_64HI] += m13 + m22 + m31;              \
    _a.u32[GU_32HI] += m30 + m21 + m12 + m03;        \
}

#endif /* GU_BYTE_ORDER */

#define GU_IMUL128_INPLACE(_a, _b) { \
    uint32_t sign = ((_a).u32[GU_32HI] ^ (_b).u32[GU_32HI]) & 0x80000000UL; \
    GU_MUL128_INPLACE (_a, _b); \
    (_a).u32[GU_32HI] |= sign; \
}

#define GU_EQ128(_a, _b) (!memcmp(&_a,&_b,sizeof(_a)))

#endif /* GU_WORDSIZE */

#endif /* _gu_int128_h_ */
