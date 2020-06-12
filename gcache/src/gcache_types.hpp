/*
 * Copyright (C) 2016-2020 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_TYPES__
#define __GCACHE_TYPES__

#include "gcache_seqno.hpp"

#include "gu_deqmap.hpp"

namespace gcache
{
    typedef gu::DeqMap<seqno_t, const void*> seqno2ptr_t;
    typedef seqno2ptr_t::iterator            seqno2ptr_iter_t;

} /* namespace gcache */

#endif /* __GCACHE_TYPES__ */
