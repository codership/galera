/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_BUFHEAD__
#define __GCACHE_BUFHEAD__

#include <cstring>
#include <stdint.h>

#include "SeqnoNone.hpp"

namespace gcache
{
    static uint64_t const BUFFER_RELEASED = 1LL << 0;
    static uint64_t const BUFFER_CANCELED = 1LL << 1;

    enum StorageType
    {
        BUFFER_IN_RAM,
        BUFFER_IN_RB,
        BUFFER_IN_PAGE
    };
    
    struct BufferHeader
    {
        ssize_t  size; /*! total buffer size, including header */
        int64_t  seqno;
        void*    ctx;
        uint32_t flags;
        int32_t  store;
    }__attribute__((__packed__));

#define BH_cast(bh) reinterpret_cast<BufferHeader*>(bh)

    static inline BufferHeader*
    ptr2BH (const void* ptr)
    {
        return (static_cast<BufferHeader*>(const_cast<void*>(ptr)) - 1);
    }

    static inline void
    BH_clear (BufferHeader* bh)
    {
        memset (bh, 0, sizeof(BufferHeader));
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
