/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "Lock.hpp"
#include "SeqnoNone.hpp"
#include "BufferHeader.hpp"
#include "GCache.hpp"

namespace gcache
{
    // returns pointer to buffer data area or 0 if no space found
    void*
    GCache::get_new_buffer (size_t size)
    {
        // reserve space for the closing header (this->next)
        ssize_t const size_next = size + sizeof(BufferHeader);

        uint8_t* ret = next;

        if (ret > first) {
            // try to find space at the end
            if ((end - ret) >= size_next) {
                goto found_space;
            }
            else {
                // no space at the end, go from the start
                ret = start;
            }
        }

        while ((first - ret) < size_next) {
            // try to discard first buffer to get more space
            BufferHeader* bh = BH(first);

            // this will be automatically true also when (first == next)
            if (!BH_is_released(bh) ||
                ((seqno_locked != SEQNO_NONE) && (bh->seqno >= seqno_locked)))
                return 0; // can't free any more space, so no buffer

            if (bh->seqno != SEQNO_NONE) {
                seqno2ptr.erase (bh->seqno);
            }

            first = first + bh->size;

            if (0 == (BH(first))->size && first != next) {
                // empty header and not next: check if we fit at the end
                // and roll over
                first  = start;
                if ((end - ret) >= size_next) {
                    goto found_space;
                }
                else {
                    ret = start;
                }
            }
        }

#ifndef NDEBUG
        if ((first - ret) < size_next) {
            log_fatal << "Assertion ((first - ret) < size_next) failed: "
                      << std::endl
                      << "first offt = " << (first - start) << std::endl
                      << "next  offt = " << (next  - start) << std::endl
                      << "end   offt = " << (end   - start) << std::endl
                      << "ret   offt = " << (ret   - start) << std::endl
                      << "size_next  = " << size_next       << std::endl;
            abort();
        }
#endif

    found_space:
        next = ret + size;
        BH_clear (BH (next));

        BufferHeader* bh = BH(ret);
        bh->size  = size;
        bh->seqno = SEQNO_NONE;
        bh->flags = 0;

        return (bh + 1); // pointer to data area
    }

    void* 
    GCache::malloc (size_t size)
    {
        size = size + sizeof(BufferHeader);

        // We can reliably allocate continuous buffer which is twice as small
        // as total cache area. So
        if (size < (size_cache >> 1)) {
            Lock  lock(mtx);
            void* ptr;

            while (0 == (ptr = get_new_buffer (size))) {
                lock.wait(cond); // wait until more memory is freed
            }

            return ptr;
        }

        return 0; // "out of memory"
    }

    void
    GCache::free (void* ptr)
    {
    }

    void*
    GCache::realloc (void* ptr, size_t size)
    {
        return 0;
    }

}
