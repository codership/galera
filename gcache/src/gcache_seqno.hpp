/*
 * Copyright (C) 2016-2020 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_SEQNO__
#define __GCACHE_SEQNO__

#include <stdint.h>
#include <limits>

namespace gcache
{
    typedef int64_t seqno_t;

    static seqno_t const SEQNO_NONE =  0;
    static seqno_t const SEQNO_ILL  = -1;
    static seqno_t const SEQNO_MAX
#if __GNUC__ <= 5  /* workaround for GCC 5 (and below) bug */
                                    __attribute__((unused))
#endif
                                    = std::numeric_limits<seqno_t>::max();

} /* namespace gcache */

#endif /* __GCACHE_SEQNO__ */
