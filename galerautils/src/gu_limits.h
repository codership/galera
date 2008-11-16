// Copyright (C) 2008 Codership Oy <info@codership.com>

/**
 * @file system limit macros
 *
 * $Id$
 */

#ifndef _gu_limits_h_
#define _gu_limits_h_

#include <unistd.h>

#define GU_PAGE_SIZE    (sysconf (_SC_PAGESIZE))
#define GU_PHYS_PAGES   (sysconf (_SC_PHYS_PAGES))
#define GU_AVPHYS_PAGES (sysconf (_SC_AVPHYS_PAGES))
#define GU_ULONG_MAX    (sysconf (_SC_ULONG_MAX))
#define GU_LONG_MAX     (GU_ULONG_MAX >> 1)

#endif /* _gu_limits_h_ */
