/*
 * Copyright (C) 2013-2020 Codership Oy <info@codership.com>
 *
 * @file Interface to CRC-32C implementations
 *
 * $Id$
 */

#ifndef _GU_CRC32C_H_
#define _GU_CRC32C_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "gu_macros.h"

#include <stdint.h> // uint32_t
#include <unistd.h> // size_t

/*! Call this to configure CRC32C to use the best available implementation */
extern void
gu_crc32c_configure();

typedef uint32_t gu_crc32c_t;

static gu_crc32c_t const GU_CRC32C_INIT = 0xFFFFFFFF;

typedef gu_crc32c_t (*gu_crc32c_func_t) (gu_crc32c_t crc,
                                         const void* data,
                                         size_t      length);

extern gu_crc32c_func_t gu_crc32c_func;

static GU_FORCE_INLINE void
gu_crc32c_init (gu_crc32c_t* crc)
{
    *crc = GU_CRC32C_INIT;
}

static GU_FORCE_INLINE void
gu_crc32c_append (gu_crc32c_t* crc, const void* data, size_t size)
{
    *crc = gu_crc32c_func (*crc, data, size);
}

static GU_FORCE_INLINE uint32_t
gu_crc32c_get (gu_crc32c_t crc)
{
    return (~(crc));
}

static GU_FORCE_INLINE uint32_t
gu_crc32c (const void* data, size_t size)
{
    return (~(gu_crc32c_func (GU_CRC32C_INIT, data, size)));
}

/* Portable software-only CRC32-C implementations for gu_crc32c_func */
extern gu_crc32c_t
gu_crc32c_sarwate     (gu_crc32c_t state, const void* data, size_t length);
extern gu_crc32c_t
gu_crc32c_slicing_by_4(gu_crc32c_t state, const void* data, size_t length);
extern gu_crc32c_t
gu_crc32c_slicing_by_8(gu_crc32c_t state, const void* data, size_t length);

#if !defined(GU_CRC32C_NO_HARDWARE)

#if defined(__x86_64) || defined(_M_AMD64) || defined(_M_X64)
#define GU_CRC32C_X86_64
#endif

#if defined(GU_CRC32C_X86_64) || defined(__i386) || defined(_M_X86)
#define GU_CRC32C_X86
#endif

#if defined(GU_CRC32C_X86)
/* x86-based CRC32-C implementations for gu_crc32c_func */
extern gu_crc32c_t
gu_crc32c_x86(gu_crc32c_t state, const void* data, size_t length);
#if defined(GU_CRC32C_X86_64)
extern gu_crc32c_t
gu_crc32c_x86_64(gu_crc32c_t state, const void* data, size_t length);
#endif /* GU_CRC32C_X86_64 */
#endif /* GU_CRC32C_X86 */

#if defined(__aarch64__) || defined(__AARCH64__)
#define GU_CRC32C_ARM64
extern gu_crc32c_t
gu_crc32c_arm64(gu_crc32c_t state, const void* data, size_t length);
#endif /* __aarch64__ || __AARCH64__ */

#if defined(GU_CRC32C_X86) || defined(GU_CRC32C_ARM64)
/** Returns hardware-accelerated CRC32C implementation */
extern gu_crc32c_func_t gu_crc32c_hardware();
#else
#define GU_CRC32C_NO_HARDWARE 1
#endif

#endif /* !GU_CRC32C_NO_HARDWARE */

#if defined(__cplusplus)
}
#endif

#endif /* _GU_CRC32C_H_ */
