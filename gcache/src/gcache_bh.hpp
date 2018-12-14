/*
 * Copyright (C) 2009-2018 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_BUFHEAD__
#define __GCACHE_BUFHEAD__

#include "gcache_memops.hpp"
#include "gcache_seqno.hpp"
#include <gu_assert.h>
#include <gu_macros.hpp>

#include <cstring>
#include <stdint.h>
#include <ostream>

namespace gcache
{
    static uint16_t const BUFFER_RELEASED  = 1 << 0;
    static uint16_t const BUFFER_SKIPPED   = 1 << 1;
    static uint16_t const BUFFER_FLAGS_MAX = (uint32_t(BUFFER_SKIPPED)<<1) - 1;

    enum StorageType
    {
        BUFFER_IN_MEM,
        BUFFER_IN_RB,
        BUFFER_IN_PAGE
    };

    typedef uint64_t BH_ctx_t;

    struct BufferHeader
    {
        int64_t  seqno_g;
        BH_ctx_t ctx;
        uint32_t size;  /*! total buffer size, including header */
        uint16_t flags;
        int8_t   store;
        int8_t   type;  /*! arbitrary user defined type */
    }__attribute__((__packed__));

    GU_COMPILE_ASSERT(sizeof(BufferHeader().size) >= sizeof(MemOps::size_type),
                      buffer_header_size_check);
    GU_COMPILE_ASSERT((sizeof(BufferHeader) % MemOps::ALIGNMENT) == 0,
                      buffer_header_alignment_check);

    /*! must store pointer on both 32 and 64-bit systems */
    GU_COMPILE_ASSERT(sizeof(BufferHeader().ctx) >= sizeof(void*),
                      buffer_header_ctx_check);

#define BH_cast(ptr) reinterpret_cast<BufferHeader*>(ptr)
#define BH_const_cast(ptr) reinterpret_cast<const BufferHeader*>(ptr)

    static inline BufferHeader*
    ptr2BH (const void* ptr)
    {
        return (static_cast<BufferHeader*>(const_cast<void*>(ptr)) - 1);
    }

    static inline void
    BH_clear (BufferHeader* const bh)
    {
        ::memset(bh, 0, sizeof(BufferHeader));
    }

    static inline bool
    BH_is_clear (const BufferHeader* const bh)
    {
        static const uint8_t clear_bh[sizeof(BufferHeader)] = { 0, };
        return (0 == ::memcmp(bh, clear_bh, sizeof(BufferHeader)));
    }

    static inline void
    BH_assert_clear (const BufferHeader* const bh)
    {
        assert(0 == bh->seqno_g);
        assert(0 == bh->size);
        assert(0 == bh->ctx);
        assert(0 == bh->flags);
        assert(0 == bh->store);
        assert(0 == bh->type);
    }

    static inline bool
    BH_is_released (const BufferHeader* const bh)
    {
        return (bh->flags & BUFFER_RELEASED);
    }

    static inline bool
    BH_is_skipped (const BufferHeader* const bh)
    {
        return (bh->flags & BUFFER_SKIPPED);
    }

    static inline MemOps*
    BH_ctx (const BufferHeader* const bh)
    {
        return reinterpret_cast<MemOps*>(bh->ctx);
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
        os << "addr: "      << static_cast<const void*>(bh)
           << ", seqno: "   << bh->seqno_g
           << ", size: "    << bh->size
           << ", ctx: "     << BH_ctx(bh)
           << ", flags: "   << bh->flags
           << ". store: "   << int(bh->store)
           << ", type: "    << int(bh->type);
        return os;
    }

    /* return true if ptr may point at BufferHeader */
    static inline bool
    BH_test(const void* const ptr)
    {
        const BufferHeader* const bh(static_cast<const BufferHeader*>(ptr));

        if (gu_likely(!BH_is_clear(bh)))
        {
            return (
                bh->seqno_g >= SEQNO_ILL &&
                int64_t(bh->size) >= int(sizeof(BufferHeader)) &&
                // ^^^ compare signed values for better certainty ^^^
                bh->flags   <= BUFFER_FLAGS_MAX &&
                bh->store   == BUFFER_IN_RB
            );
        }

        return true;
    }
} /* namespace gcache */

#endif /* __GCACHE_BUFHEAD__ */
