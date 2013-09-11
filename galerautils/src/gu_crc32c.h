/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _GU_CRC32C_H_
#define _GU_CRC32C_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "www.evanjones.ca/crc32c.h"

/*! Call this to configure CRC32C to use the best available implementation */
extern void
gu_crc32c_configure();

extern CRC32CFunctionPtr gu_crc32c_func;

typedef uint32_t gu_crc32c_t;

static inline void
gu_crc32c_init (gu_crc32c_t* crc)
{
    *crc = 0xffffffff;
}

static inline void
gu_crc32c_append (gu_crc32c_t* crc, const void* data, size_t size)
{
    *crc = gu_crc32c_func (*crc, data, size);
}

static inline uint32_t
gu_crc32c_get (gu_crc32c_t* crc)
{
    return ~(*crc);
}

static inline uint32_t
gu_crc32c (const void* data, size_t size)
{
    return ~(gu_crc32c_func (0xffffffff, data, size));
}

#if defined(__cplusplus)
}
#endif

#endif /* _GU_CRC32C_H_ */
