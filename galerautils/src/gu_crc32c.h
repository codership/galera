/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * @file Interface to CRC-32C implementation from www.evanjones.ca
 *
 * $Id$
 */

#ifndef _GU_CRC32C_H_
#define _GU_CRC32C_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "www.evanjones.ca/crc32c.h"
#include "gu_macros.h"

/*! Call this to configure CRC32C to use the best available implementation */
extern void
gu_crc32c_configure();

extern CRC32CFunctionPtr gu_crc32c_func;

typedef uint32_t gu_crc32c_t;

static gu_crc32c_t const GU_CRC32C_INIT = 0xFFFFFFFF;

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
    return ~(crc);
}

static GU_FORCE_INLINE uint32_t
gu_crc32c (const void* data, size_t size)
{
    return ~(gu_crc32c_func (GU_CRC32C_INIT, data, size));
}

#if defined(__cplusplus)
}
#endif

#endif /* _GU_CRC32C_H_ */
