/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_BUFHEAD__
#define __GCACHE_BUFHEAD__

#include <cstring>
#include <stdint.h>
#include <ostream>

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
        int64_t  size;    /*! total buffer size, including header */
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
    BH_clear (BufferHeader* const bh)
    {
        memset (bh, 0, sizeof(BufferHeader));
    }

    static inline void
    BH_assert_clear (const BufferHeader* const bh)
    {
        assert(0 == bh->seqno_g);
        assert(0 == bh->seqno_d);
        assert(0 == bh->size);
        assert(0 == bh->ctx);
        assert(0 == bh->flags);
        assert(0 == bh->store);
    }

    static inline bool
    BH_is_released (const BufferHeader* const bh)
    {
        return (bh->flags & BUFFER_RELEASED);
    }

    static inline void
    BH_release (BufferHeader* const bh)
    {
        assert(!BH_is_released(bh));
        bh->flags |= BUFFER_RELEASED;
    }

    static inline BufferHeader* BH_next(BufferHeader* bh)
    {
        return BH_cast((reinterpret_cast<uint8_t*>(bh) + bh->size));
    }

    static inline std::ostream&
    operator << (std::ostream& os, const BufferHeader* const bh)
    {
        os << "seqno_g: "   << bh->seqno_g
           << ", seqno_d: " << bh->seqno_d
           << ", size: "    << bh->size
           << ", ctx: "     << bh->ctx
           << ", flags: "   << bh->flags
           << ". store: "   << bh->store;
        return os;
    }

}

#endif /* __GCACHE_BUFHEAD__ */
