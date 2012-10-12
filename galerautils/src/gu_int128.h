// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file 128-bit arithmetic macros. This is so far needed only for FNV128
 *       hash algorithm
 *
 * $Id$
 */

#ifndef _gu_int128_h_
#define _gu_int128_h_

#include "gu_arch.h"
#include "gu_byteswap.h"

#include <stdint.h>

#if defined(__SIZEOF_INT128__)

typedef          int __attribute__((__mode__(__TI__)))  int128_t;
typedef unsigned int __attribute__((__mode__(__TI__))) uint128_t;

typedef int128_t  gu_int128_t;
typedef uint128_t gu_uint128_t;

#define GU_SET128(_a, hi64, lo64) _a = (((uint128_t)hi64) << 64) + lo64
#define GU_MUL128_INPLACE(_a, _b) _a *= _b
#define GU_IMUL128_INPLACE(_a, _b) GU_MUL128_INPLACE(_a, _b)
#define GU_EQ128(_a, _b) (_a == _b)

#else /* Uncapable of 16-byte integer arythmetic */

#if defined(GU_LITTLE_ENDIAN)

#define GU_64LO 0
#define GU_64HI 1
#define GU_32LO 0
#define GU_32HI 3
#define GU_32_0 0
#define GU_32_1 1
#define GU_32_2 2
#define GU_32_3 3

typedef union gu_int128 {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {uint32_t lo; uint64_t mid; int32_t hi;}__attribute__((packed)) m;
#ifdef __cplusplus
    gu_int128() : m() {}
    gu_int128(int64_t hi, uint64_t lo) : m() { u64[0] = lo; u64[1] = hi; }
#endif
} gu_int128_t;

typedef union gu_uint128 {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {uint32_t lo; uint64_t mid; uint32_t hi;}__attribute__((packed)) m;
#ifdef __cplusplus
    gu_uint128() : m() {}
    gu_uint128(uint64_t hi, uint64_t lo) : m() { u64[0] = lo; u64[1] = hi; }
#endif
} gu_uint128_t;

#ifdef __cplusplus
#define GU_SET128(_a, hi64, lo64) _a = gu_uint128(hi64, lo64)
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

#else /* Big-Endian */

#define GU_64HI 0
#define GU_64LO 1
#define GU_32HI 0
#define GU_32LO 3

typedef union gu_int128 {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {int32_t hi; uint64_t mid; uint32_t lo;}__attribute__((packed)) m;
#ifdef __cplusplus
    gu_int128() {}
    gu_int128(int64_t hi, uint64_t lo) { u64[0] = hi; u64[1] = lo; }
#endif
} gu_int128_t;

typedef union gu_uint128 {
    uint64_t u64[2];
    uint32_t u32[4];
    struct {uint32_t hi; uint64_t mid; uint32_t lo;}__attribute__((packed)) m;
#ifdef __cplusplus
    gu_uint128() {}
    gu_uint128(uint64_t hi, uint64_t lo) { u64[0] = hi; u64[1] = lo; }
#endif
} gu_uint128_t;

#ifdef __cplusplus
#define GU_SET128(_a, hi64, lo64) _a = gu_uint128(hi64, lo64)
#else
#define GU_SET128(_a, hi64, lo64) _a = { .u64 = { hi64, lo64 } }
#endif

#define GU_MUL128_INPLACE(_a,_b) {                   \
    uint64_t m33 = (uint64_t)_a.u32[3] * _b.u32[3];  \
    uint64_t m23 = (uint64_t)_a.u32[2] * _b.u32[3];  \
    uint64_t m13 = (uint64_t)_a.u32[1] * _b.u32[3];  \
    uint64_t m32 = (uint64_t)_a.u32[3] * _b.u32[2];  \
    uint64_t m31 = (uint64_t)_a.u32[3] * _b.u32[1];  \
    uint64_t m22 = (uint64_t)_a.u32[2] * _b.u32[2];  \
    uint32_t m30 = _a.u32[3] * _b.u32[0];            \
    uint32_t m21 = _a.u32[2] * _b.u32[1];            \
    uint32_t m12 = _a.u32[1] * _b.u32[2];            \
    uint32_t m03 = _a.u32[0] * _b.u32[3];            \
    _a.u64[GU_64LO]  = m00; _a.u64[GU_64HI] = 0;     \
    _a.m.mid  += m23; _a.m.hi += (_a.m.mid < m23);   \
    _a.m.mid  += m32; _a.m.hi += (_a.m.mid < m32);   \
    _a.u64[GU_64HI] += m13 + m22 + m31;              \
    _a.u32[GU_32HI] += m30 + m21 + m12 + m03;        \
}

#endif /* Big-Endian */

#define GU_IMUL128_INPLACE(_a, _b) {                                    \
    uint32_t sign = ((_a).u32[GU_32HI] ^ (_b).u32[GU_32HI]) & 0x80000000UL; \
    GU_MUL128_INPLACE (_a, _b);                                         \
    (_a).u32[GU_32HI] |= sign;                                          \
}

#define GU_EQ128(_a, _b) (!memcmp(&_a,&_b,sizeof(_a)))

#endif /* __SIZEOF_INT128__ */

/* Not sure how to make it both portable, efficient and still follow the
 * signature of other byteswap functions at the same time.
 * So this one does inplace conversion. */

#ifdef __cplusplus
extern "C" {
#endif

static inline void
gu_bswap128 (gu_uint128_t* const arg)
{
    uint64_t* x = (uint64_t*)arg;
    uint64_t tmp = gu_bswap64(x[0]);
    x[0] = gu_bswap64(x[1]);
    x[1] = tmp;
}

#ifdef __cplusplus
}
#endif

#ifdef GU_LITTLE_ENDIAN
#  define gu_le128(x) {}
#  define gu_be128(x) gu_bswap128(x)
#else
#  define gu_le128(x) gu_bswap128(x)
#  define gu_be128(x) {}
#endif /* GU_LITTLE_ENDIAN */

#define htog128(x) gu_le128(x)
#define gtoh128(x) htog128(x)

#endif /* _gu_int128_h_ */
