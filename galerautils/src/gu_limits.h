// Copyright (C) 2008 Codership Oy <info@codership.com>

/**
 * @file system limit macros
 *
 * $Id$
 */

#ifndef _gu_limits_h_
#define _gu_limits_h_

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif
# if defined(__APPLE__)
long gu_darwin_phys_pages (void);
long gu_darwin_avphys_pages (void);
# elif defined(__FreeBSD__)
long gu_freebsd_avphys_pages (void);
# endif
#ifdef __cplusplus
}
#endif

#if defined(__APPLE__)
# define GU_PAGE_SIZE    (getpagesize ())
# define GU_PHYS_PAGES   (gu_darwin_phys_pages ())
# define GU_AVPHYS_PAGES (gu_darwin_avphys_pages ())
#elif defined(__FreeBSD__)
# define GU_PAGE_SIZE    (sysconf (_SC_PAGESIZE))
# define GU_PHYS_PAGES   (sysconf (_SC_PHYS_PAGES))
# define GU_AVPHYS_PAGES (gu_freebsd_avphys_pages ())
#else /* !__APPLE__ && !__FreeBSD__ */
# define GU_PAGE_SIZE    (sysconf (_SC_PAGESIZE))
# define GU_PHYS_PAGES   (sysconf (_SC_PHYS_PAGES))
# define GU_AVPHYS_PAGES (sysconf (_SC_AVPHYS_PAGES))
#endif

#define GU_AVPHYS_SIZE  (((unsigned long long)GU_AVPHYS_PAGES)*GU_PAGE_SIZE)

#include <limits.h>
#define GU_ULONG_MAX      ULONG_MAX
#define GU_LONG_MAX       LONG_MAX
#define GU_LONG_MIN       LONG_MIN
#define GU_ULONG_LONG_MAX ULLONG_MAX
#define GU_LONG_LONG_MAX  LLONG_MAX
#define GU_LONG_LONG_MIN  LLONG_MIN

#endif /* _gu_limits_h_ */
