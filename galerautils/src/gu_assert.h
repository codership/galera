// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Assert macro definition
 *
 * $Id$
 */

#ifndef _gu_assert_h_
#define _gu_assert_h_

#include "gu_log.h"

#ifndef DEBUG_ASSERT
#include <assert.h>
#else
#include <unistd.h>
#undef assert
/** Assert that sleeps instead of aborting the program, saving it for gdb */
#define assert(expr)  if (!(expr)) { \
                      gu_fatal ("Assertion (%s) failed", __STRING(expr)); \
                      while(1) sleep(1); }
#endif /* DEBUG_ASSERT */

#endif /* _gu_assert_h_ */
