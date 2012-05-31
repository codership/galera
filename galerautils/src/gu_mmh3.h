// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file MurMurHash3 header
 *
 * $Id$
 */

#ifndef _gu_mmh3_h_
#define _gu_mmh3_h_

#include "gu_arch.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
