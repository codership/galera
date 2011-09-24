/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_BUFHEAD__
#define __GCACHE_BUFHEAD__

#include <cstring>
#include <stdint.h>

#include "SeqnoNone.hpp"
#include "gcache_memops.hpp"

namespace gcache
{
    static uint32_t const BUFFER_RELEASED  = 1 << 0;

    enum StorageType
    {
        BUFFER_IN_MEM,
        BUFFER_IN_RB,
        BUFFER_IN_PAGE
    };

    struct BufferHeader
    {
        int64_t  seqno_g;
        int64_t  seqno_d;
        ssize_t  size;    /*! total buffer size, including header */
        MemOps*  ctx;
        uint32_t flags;
        int32_t  store;
    }__attribute__((__packed__));

#define BH_cast(ptr) reinterpret_cast<BufferHeader*>(ptr)

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

#if REMOVE
    static inline void
    BH_cancel (BufferHeader* bh)
    { bh->flags |= BUFFER_CANCELED; }

    static inline bool
    BH_is_canceled (BufferHeader* bh)
    { return (bh->flags & BUFFER_CANCELED); }
#endif
}

#endif /* __GCACHE_BUFHEAD__ */
