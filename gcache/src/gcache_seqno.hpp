/*
 * Copyright (C) 2016-2025 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_SEQNO__
#define __GCACHE_SEQNO__

#include <wsrep_api.h>
#include <limits>

namespace gcache
{
    typedef wsrep_seqno_t seqno_t;

    static seqno_t const SEQNO_NONE =  0;
    static seqno_t const SEQNO_ILL  = -1;
    static seqno_t const SEQNO_MAX
#if __GNUC__ <= 5  /* workaround for GCC 5 (and below) bug */
                                    __attribute__((unused))
#endif
                                    = std::numeric_limits<seqno_t>::max();
    /* Protected interface to seqno map */
    class SeqnoMap
    {
    public:
        /* Discards all seqnos upto and including seqno */
        virtual void seqno_discard(const seqno_t& seqno) = 0;
        /* Set low limit on available senqos to > seqno
         * (global lock should be held while it is called) */
        virtual void set_low_limit(const seqno_t& seqno) = 0;

        virtual ~SeqnoMap() {}
    }; /* SeqnoMap */

} /* namespace gcache */

#endif /* __GCACHE_SEQNO__ */
