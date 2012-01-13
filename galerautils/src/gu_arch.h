// Copyright (C) 2012 Codership Oy <info@codership.com>

/**
 * @file CPU architecture related functions/macros
 *
 * $Id$
 */

#ifndef _gu_arch_h_
#define _gu_arch_h_

#if defined(HAVE_SYS_ENDIAN_H)
# include <sys/endian.h>
#else
# include <endian.h>
#endif

#if !defined(_BYTE_ORDER) && !defined(__BYTE_ORDER)
# error "Byte order not defined"
#endif

#if defined(__BYTE_ORDER)
# define GU_BYTE_ORDER    __BYTE_ORDER
# define GU_LITTLE_ENDIAN __LITTLE_ENDIAN
# define GU_BIG_ENDIAN    __BIG_ENDIAN
#else
# define GU_BYTE_ORDER     _BYTE_ORDER
# define GU_LITTLE_ENDIAN  _LITTLE_ENDIAN
# define GU_BIG_ENDIAN     _BIG_ENDIAN
#endif

#include <bits/wordsize.h>

#define GU_WORDSIZE __WORDSIZE

#if (GU_WORDSIZE != 32) && (GU_WORDSIZE != 64)
# error "Unsupported wordsize"
#endif

#endif /* _gu_arch_h_ */
