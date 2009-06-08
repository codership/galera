// Copyright (C) 2009 Codership Oy <info@codership.com>

/**
 * @file Assert macro definition
 *
 * $Id$
 */

#ifndef _gu_assert_hpp_
#define _gu_assert_hpp_

#include "gu_logger.hpp"

#ifndef DEBUG_ASSERT
#include <cassert>
#else
#include <unistd.h>
#undef assert
/** Assert that sleeps instead of aborting the program, saving it for gdb */
#define assert(expr) \
    if (!(expr)) {                                                      \
        gu::log_fatal << "Assertion (" << __STRING(expr) << ") failed"; \
    while(1) sleep(1); }
#endif /* DEBUG_ASSERT */

#endif /* _gu_assert_hpp_ */
