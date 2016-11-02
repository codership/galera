/*
 * Copyright (C) 2016 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_TYPES__
#define __GCACHE_TYPES__

#include "gcache_seqno.hpp"
#include <map>

namespace gcache
{
    typedef std::map<seqno_t, const void*>  seqno2ptr_t;
    typedef seqno2ptr_t::iterator           seqno2ptr_iter_t;
    typedef std::pair<seqno_t, const void*> seqno2ptr_pair_t;

} /* namespace gcache */

#endif /* __GCACHE_TYPES__ */
