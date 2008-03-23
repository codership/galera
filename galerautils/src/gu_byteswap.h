// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Byte swapping functions/macros
 *
 * $Id$
 */

#ifndef _gu_byteswap_h_
#define _gu_byteswap_h_

/* GNU libc has them hardware optimized */
#include <endian.h>   // for __BYTE_ORDER et al.
#include <byteswap.h> // for bswap_16(x), bswap_32(x), bswap_64(x) 

/* @note: there are inline functions behind these macros,
 *        so typesafety is taken care of. */

#if   __BYTE_ORDER == __LITTLE_ENDIAN

/* convert to/from Little Endian representation */
#define gu_le16(x) (x)
#define gu_le32(x) (x)
#define gu_le64(x) (x)

/* convert to/from Big Endian representation */
#define gu_be16(x) bswap_16(x) 
#define gu_be32(x) bswap_32(x)
#define gu_be64(x) bswap_64(x)

#elif __BYTE_ORDER == __BIG_ENDIAN

/* convert to/from Little Endian representation */
#define gu_le16(x) bswap_16(x) 
#define gu_le32(x) bswap_32(x)
#define gu_le64(x) bswap_64(x)

/* convert to/from Big Endian representation */
#define gu_be16(x) (x)
#define gu_be32(x) (x)
#define gu_be64(x) (x)

#else

#error "Byte order unrecognized!"

#endif /* __BYTE_ORDER */

/* Analogues to htonl and friends. Since we'll be dealing mostly with
 * little-endian architectures, there is more sense to use little-endian
 * as default */
#define htogs(x) gu_le16(x)
#define gtohs(x) htogs(x)
#define htogl(x) gu_le32(x)
#define gtohl(x) htogl(x)
 
#endif /* _gu_byteswap_h_ */
