/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_SEQNO_NONE__
#define __GCACHE_SEQNO_NONE__

//#include <tr1/cstdint>
#include <stdint.h>

namespace gcache
{
    static int64_t const SEQNO_NONE = 0;
    static int64_t const SEQNO_ILL  = -1;
}

#endif /* __GCACHE_SEQNO_NONE__ */
