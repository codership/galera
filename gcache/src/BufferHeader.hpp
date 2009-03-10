/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_BUFHEAD__
#define __GCACHE_BUFHEAD__

#include <cstring>
#include <tr1/cstdint>

namespace gcache
{
    static uint64_t const BUFFER_RELEASED = 1 << 0;
    static uint64_t const BUFFER_CANCELED = 1 << 1;

    struct BufferHeader
    {
        uint64_t size;
        int64_t  seqno;
        uint64_t flags;
    }__attribute__((__packed__));

    static inline BufferHeader*
    BH(void* ptr) { return static_cast<BufferHeader*>(ptr); };

    static inline void
    BH_zero (void* ptr) { memset (ptr, 0, sizeof(BufferHeader)); };

    static inline bool
    BH_released (void* ptr)
    { return (((static_cast<BufferHeader*>(ptr))->flags) & BUFFER_RELEASED); }
}

#endif /* __GCACHE_BUFHEAD__ */
