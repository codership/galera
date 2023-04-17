/*
 * Copyright (C) 2016-2021 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_TYPES__
#define __GCACHE_TYPES__

#include "gcache_seqno.hpp"

#include "gu_deqmap.hpp"
#include "gu_progress.hpp"

namespace gcache
{
    typedef int64_t progress_t; //should be sufficient for all kinds of progress
    typedef gu::Progress<progress_t>::Callback ProgressCallback;

    typedef gu::DeqMap<seqno_t, const void*> seqno2ptr_t;
    typedef seqno2ptr_t::iterator            seqno2ptr_iter_t;

} /* namespace gcache */

#endif /* __GCACHE_TYPES__ */
