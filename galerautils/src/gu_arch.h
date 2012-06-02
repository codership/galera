// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file CPU architecture related functions/macros
 *
 * $Id$
 */

#ifndef _gu_arch_h_
#define _gu_arch_h_

#if defined(HAVE_ENDIAN_H)
# include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
# include <sys/endian.h>
#elif defined(HAVE_SYS_BYTEORDER_H)
# include <sys/byteorder.h>
#else
# error "No byte order header file detected"
#endif

#if defined(__BYTE_ORDER)
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define GU_LITTLE_ENDIAN
# endif
#elif defined(_BYTE_ORDER)
# if _BYTE_ORDER == _LITTLE_ENDIAN
#  define GU_LITTLE_ENDIAN
# endif
#elif defined(__sun__)
# if !defined(_BIG_ENDIAN)
#  define GU_LITTLE_ENDIAN
# endif
#else
# error "Byte order not defined"
#endif

#if defined(__sun__)
# define GU_WORDSIZE 64 /* Solaris 11 is only 64-bit ATM */
#else
# include <bits/wordsize.h>
# define GU_WORDSIZE __WORDSIZE
#endif

#if (GU_WORDSIZE != 32) && (GU_WORDSIZE != 64)
# error "Unsupported wordsize"
#endif

/* I'm not aware of the platforms that don't, but still */
#define GU_ALLOW_UNALIGNED_READS 1

#endif /* _gu_arch_h_ */
