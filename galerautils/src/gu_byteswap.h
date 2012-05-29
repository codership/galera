// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file Byte swapping functions/macros
 *
 * $Id$
 */

#ifndef _gu_byteswap_h_
#define _gu_byteswap_h_

#include "gu_arch.h"

#if defined(HAVE_BYTESWAP_H)
# include <byteswap.h> // for bswap_16(x), bswap_32(x), bswap_64(x)
#endif /* HAVE_BYTESWAP_H */

#if defined(bswap16)
#  define gu_bswap16 bswap16
#  define gu_bswap32 bswap32
#  define gu_bswap64 bswap64
#elif defined(bswap_16)
#  define gu_bswap16 bswap_16
#  define gu_bswap32 bswap_32
#  define gu_bswap64 bswap_64
#elif defined(__sun__) /* BSWAP macros inherited from gu_arch.h */
#  define gu_bswap16 BSWAP_16
#  define gu_bswap32 BSWAP_32
#  define gu_bswap64 BSWAP_64
#else
#  error "No byteswap macros are defined"
#endif

/* @note: there are inline functions behind these macros,
 *        so typesafety is taken care of... However C++ still has issues: */
#ifdef __cplusplus
// To pacify C++. Not loosing much optimization on 2 bytes anyways.
#include <stdint.h>
#undef gu_bswap16
static inline uint16_t gu_bswap16(uint16_t const x)
// Even though x is declared as 'uint16_t', g++-4.4.1 still treats results
// of operations with it as 'int' and freaks out on return with -Wconversion.
{ return static_cast<uint16_t>((x >> 8) | (x << 8)); }
#endif // __cplusplus

#if defined(GU_LITTLE_ENDIAN)
/* convert to/from Little Endian representation */
#define gu_le16(x) (x)
#define gu_le32(x) (x)
#define gu_le64(x) (x)

/* convert to/from Big Endian representation */
#define gu_be16(x) gu_bswap16(x)
#define gu_be32(x) gu_bswap32(x)
#define gu_be64(x) gu_bswap64(x)

#else /* Big-Endian */

/* convert to/from Little Endian representation */
#define gu_le16(x) gu_bswap16(x)
#define gu_le32(x) gu_bswap32(x)
#define gu_le64(x) gu_bswap64(x)

/* convert to/from Big Endian representation */
#define gu_be16(x) (x)
#define gu_be32(x) (x)
#define gu_be64(x) (x)

#endif /* Big-Endian */

/* Analogues to htonl and friends. Since we'll be dealing mostly with
 * little-endian architectures, there is more sense to use little-endian
 * as default */
#define htogs(x) gu_le16(x)
#define gtohs(x) htogs(x)
#define htogl(x) gu_le32(x)
#define gtohl(x) htogl(x)

/* Analogues to htogs() and friends, suffixed with type width */
#define htog16(x) gu_le16(x)
#define gtoh16(x) htog16(x)
#define htog32(x) gu_le32(x)
#define gtoh32(x) htog32(x)
#define htog64(x) gu_le64(x)
#define gtoh64(x) htog64(x)

#endif /* _gu_byteswap_h_ */
