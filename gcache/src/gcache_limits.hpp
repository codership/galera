/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#ifndef _gcache_limits_hpp_
#define _gcache_limits_hpp_

#include "gcache_bh.hpp"
#include <gu_macros.hpp> // GU_COMPILE_ASSERT
#include <limits>

namespace gcache
{
    class Limits
    {
    public:

        typedef MemOps::size_type  size_type;
        typedef MemOps::ssize_type ssize_type;

        static ssize_type const SSIZE_MAX_ =
            (1ULL << (sizeof(ssize_type)*8 - 1)) - 1;

        static size_type const MAX_SIZE = sizeof(BufferHeader) + SSIZE_MAX_;

        static size_type const MIN_SIZE = sizeof(BufferHeader) + 1;

        static inline void assert_size(unsigned long long s)
        {
#ifndef NDEBUG
            assert(s <= MAX_SIZE);
            assert(s >= MIN_SIZE);
#endif /* NDEBUG */
        }

    private:

        /* the difference between MAX_SIZE and MIN_SIZE should never exceed
         * diff_type capacity */

        GU_COMPILE_ASSERT(MAX_SIZE > MIN_SIZE, max_min);

        typedef MemOps::diff_type diff_type;

        static diff_type const DIFF_MAX =
            (1ULL << (sizeof(diff_type)*8 - 1)) - 1;

        GU_COMPILE_ASSERT(DIFF_MAX >= 0, diff_max);
        GU_COMPILE_ASSERT(size_type(DIFF_MAX) >= MAX_SIZE - MIN_SIZE, max_diff);

        static diff_type const DIFF_MIN = -DIFF_MAX - 1;

        typedef long long long_long;

        GU_COMPILE_ASSERT(DIFF_MIN < 0, diff_min);
        GU_COMPILE_ASSERT(DIFF_MIN + MAX_SIZE <= MIN_SIZE, min_diff);

    }; /* class Limits */
}

#endif /* _gcache_limits_hpp_ */
