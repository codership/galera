// Copyright (C) 2008-2016 Codership Oy <info@codership.com>

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

extern size_t gu_page_size(void);
extern size_t gu_phys_pages(void);
extern size_t gu_avphys_pages(void);

#ifdef __cplusplus
} // extern "C"
#endif

#define GU_PAGE_SIZE gu_page_size()

/* returns multiple of page size that is no less than page size */
static inline size_t gu_page_size_multiple(size_t const requested_size)
{
    size_t const sys_page_size = GU_PAGE_SIZE;
    size_t const multiple = requested_size / sys_page_size;
    return sys_page_size * (0 == multiple ? 1 : multiple);
}

static inline size_t gu_avphys_bytes()
{
    // to detect overflow on systems with >4G of RAM, see #776
    unsigned long long avphys = gu_avphys_pages(); avphys *= gu_page_size();
    size_t max = -1;
    return (avphys < max ? avphys : max);
}

#include <limits.h>

#define GU_ULONG_MAX  ULONG_MAX
#define GU_LONG_MAX   LONG_MAX
#define GU_LONG_MIN   LONG_MIN

#ifdef ULLONG_MAX
#define GU_ULLONG_MAX ULLONG_MAX
#define GU_LLONG_MAX  LLONG_MAX
#define GU_LLONG_MIN  LLONG_MIN
#else
#define GU_ULLONG_MAX 0xffffffffffffffffULL
#define GU_LLONG_MAX  0x7fffffffffffffffLL
#define GU_LLONG_MIN  (-GU_LONG_LONG_MAX - 1)
#endif

#define GU_MIN_ALIGNMENT 8

#endif /* _gu_limits_h_ */
