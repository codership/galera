// Copyright (C) 2012-2017 Codership Oy <info@codership.com>

/**
 * @file CPU architecture related functions/macros
 *
 * $Id$
 */

#ifndef _gu_arch_h_
#define _gu_arch_h_

#if defined(HAVE_ENDIAN_H)
# include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H) /* FreeBSD */
# include <sys/endian.h>
#elif defined(HAVE_SYS_BYTEORDER_H)
# include <sys/byteorder.h>
#elif defined(__APPLE__)
# include <machine/endian.h>
#else
# error "No byte order header file detected"
#endif

#if defined(__BYTE_ORDER)
# if __BYTE_ORDER == __LITTLE_ENDIAN
#  define GU_LITTLE_ENDIAN
# endif
#elif defined(_BYTE_ORDER) /* FreeBSD */
# if _BYTE_ORDER == _LITTLE_ENDIAN
#  define GU_LITTLE_ENDIAN
# endif
#elif defined(__APPLE__) && defined(__DARWIN_BYTE_ORDER)
# if __DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN
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
# if defined (_LP64)
#  define GU_WORDSIZE 64
# else
#  define GU_WORDSIZE 32
# endif
#elif defined(__APPLE__) || defined(__FreeBSD__)
# include <stdint.h>
# define GU_WORDSIZE __WORDSIZE
#else
# include <bits/wordsize.h>
# define GU_WORDSIZE __WORDSIZE
#endif

#include <stdint.h>
#if (GU_WORDSIZE == 32)
typedef uint32_t gu_word_t;
#elif (GU_WORDSIZE == 64)
typedef uint64_t gu_word_t;
#else
# error "Unsupported wordsize"
#endif

#define GU_WORD_BYTES sizeof(gu_word_t)

#include <assert.h>
#ifdef __cpluplus // to avoid "old-style cast" in C++ make it temp instantiation
#define GU_ASSERT_ALIGNMENT(x)                              \
    assert((uintptr_t(&(x)) % sizeof(x))     == 0 ||        \
           (uintptr_t(&(x)) % GU_WORD_BYTES) == 0)
#else // ! __cplusplus
#define GU_ASSERT_ALIGNMENT(x)                              \
    assert(((uintptr_t)(&(x)) % sizeof(x))     == 0 ||      \
           ((uintptr_t)(&(x)) % GU_WORD_BYTES) == 0)
#endif // !__cplusplus

#endif /* _gu_arch_h_ */
