/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_BUFHEAD__
#define __GCACHE_BUFHEAD__

#include <cstring>
//#include <tr1/cstdint>
#include <stdint.h>

#include "SeqnoNone.hpp"

namespace gcache
{
    static uint64_t const BUFFER_RELEASED = 1 << 0;
    static uint64_t const BUFFER_CANCELED = 1 << 1;

    struct BufferHeader
    {
        ssize_t  size;
        int64_t  seqno;
        uint64_t flags;
    }__attribute__((__packed__));

    static inline BufferHeader*
    BH (void* ptr) { return static_cast<BufferHeader*>(ptr); }

    static inline void
    BH_clear (BufferHeader* bh)
    {
        bh->size  = 0;
        bh->seqno = SEQNO_NONE;
        bh->flags = 0;
    }

    static inline void
    BH_release (BufferHeader* bh)
    { bh->flags |= BUFFER_RELEASED; }

    static inline bool
    BH_is_released (BufferHeader* bh)
    { return (bh->flags & BUFFER_RELEASED); }

    static inline void
    BH_cancel (BufferHeader* bh)
    { bh->flags |= BUFFER_CANCELED; }

    static inline bool
    BH_is_canceled (BufferHeader* bh)
    { return (bh->flags & BUFFER_CANCELED); }
}

#endif /* __GCACHE_BUFHEAD__ */
