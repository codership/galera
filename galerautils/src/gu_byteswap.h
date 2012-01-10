// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Byte swapping functions/macros
 *
 * $Id$
 */

#ifndef _gu_byteswap_h_
#define _gu_byteswap_h_

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

/* GNU libc has them hardware optimized */
#if defined(HAVE_ENDIAN_H)
#include <endian.h>   // for __BYTE_ORDER et al.
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#endif

#if defined(HAVE_BYTESWAP_H)
#include <byteswap.h> // for bswap_16(x), bswap_32(x), bswap_64(x)
#endif /* GALERA_USE_BYTESWAP_H */

#if !defined(_BYTE_ORDER) && !defined(__BYTE_ORDER)
#error "byte order not defined"
#endif 

#if !defined(bswap16) && !defined(bswap_16)
#error "bswap16 or bswap_16 not defined"
#endif

#if !defined(bswap16)
#define bswap16 bswap_16
#define bswap32 bswap_32
#define bswap64 bswap_64
#endif

#if !defined(__BYTE_ORDER)
#define __BYTE_ORDER _BYTE_ORDER
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN _BIG_ENDIAN
#endif

/* @note: there are inline functions behind these macros,
 *        so typesafety is taken care of... However C++ still has issues: */

#ifdef __cplusplus
// To pacify C++. Not loosing much optimization on 2 bytes anyways.
#include <stdint.h>
#undef bswap16
static inline uint16_t bswap16(uint16_t x)
// Even though x is declared as 'uint16_t', g++-4.4.1 still treats results
// of operations with it as 'int' and freaks out on return with -Wconversion.
{ return static_cast<uint16_t>((x >> 8) | (x << 8)); }
#endif // __cplusplus

#if   __BYTE_ORDER == __LITTLE_ENDIAN
/* convert to/from Little Endian representation */
#define gu_le16(x) (x)
#define gu_le32(x) (x)
#define gu_le64(x) (x)

/* convert to/from Big Endian representation */
#define gu_be16(x) bswap16(x)
#define gu_be32(x) bswap32(x)
#define gu_be64(x) bswap64(x)

#elif __BYTE_ORDER == __BIG_ENDIAN

/* convert to/from Little Endian representation */
#define gu_le16(x) bswap16(x)
#define gu_le32(x) bswap32(x)
#define gu_le64(x) bswap64(x)

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
 
/* Analogues to htogs() and friends, named with type width */
#define htog16(x) gu_le16(x)
#define gtoh16(x) htog16(x)
#define htog32(x) gu_le32(x)
#define gtoh32(x) htog32(x)
#define htog64(x) gu_le64(x)
#define gtoh64(x) htog64(x)

#endif /* _gu_byteswap_h_ */
