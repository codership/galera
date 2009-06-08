/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include <cassert>

#include <galerautils.hpp>
#include "SeqnoNone.hpp"
#include "BufferHeader.hpp"
#include "GCache.hpp"

namespace gcache
{
    // Discarding always should happen in TO
    inline void
    GCache::discard_buffer (void* _bh) {
        BufferHeader* bh = static_cast<BufferHeader*>(_bh);
        seqno2ptr.erase (bh->seqno);
        seqno_min = bh->seqno + 1;
        bh->seqno = SEQNO_NONE;
        size_free += bh->size;
    }

    // returns pointer to buffer data area or 0 if no space found
    void*
    GCache::get_new_buffer (size_t size)
    {
        // reserve space for the closing header (this->next)
        ssize_t const size_next = size + sizeof(BufferHeader);

        // don't even try if there's not enough unused space
        if (size_next > ((ssize_t)size_cache - size_used)) return 0;

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
                // we need to discard this buffer, and therefore all buffers
                // with preceding seqnos
                int64_t seqno;
                for (seqno = seqno_min; seqno < bh->seqno; seqno++) {
                    BufferHeader* _bh = BH (seqno2ptr[seqno]);
                    if (!BH_is_released(_bh)) return 0;
                    discard_buffer (_bh);
                }

                discard_buffer (bh);
            }

            first += bh->size;

            if (0 == (BH(first))->size && first != next) {
                // empty header and not next: check if we fit at the end
                // and roll over if not
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
            log_fatal << "Assertion ((first - ret) >= size_next) failed: "
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
        size_used += size;
        size_free -= size;

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
        // as total cache area. So compare to half the space
        if (size < (size_cache >> 1)) {

            gu::Lock lock(mtx);
            void*    ptr;

            // (size_cache - size_used) is how much memory we can potentially
            // reserve right now. If it is too low, don't even try, wait.
            while (0 == (ptr = get_new_buffer (size))) {
                lock.wait(cond); // wait until more memory is freed
            }
            assert (0 != ptr);

            cond.signal(); // there might be other mallocs waiting.
            return (BH(ptr) + 1);
        }

        return 0; // "out of memory"
    }

    void
    GCache::free (void* ptr)
    {
        BufferHeader* bh = BH(ptr) - 1;
        gu::Lock      lock(mtx);

        size_used -= bh->size;
        // space is unused but not free
        // space counted as free only when it is erased from the map
        BH_release (bh);
        cond.signal();
    }

    void*
    GCache::realloc (void* ptr, size_t size)
    {
        size = size + sizeof(BufferHeader);

        // We can reliably allocate continuous buffer which is twice as small
        // as total cache area. So compare to half the space
        if (size >= (size_cache >> 1)) return 0;

        BufferHeader* bh = BH(ptr) - 1;

        // first check if we can grow this buffer by allocating
        // adjacent buffer
        {
            uint8_t* adj_ptr  = reinterpret_cast<uint8_t*>(bh) + bh->size;
            size_t   adj_size = size - bh->size;
            void*    adj_buff = 0;

            gu::Lock lock(mtx);

            // do the same stuff as in malloc(), conditional that we have
            // adjacent space
            while ((adj_ptr == next) &&
                   (0 == (adj_buff = get_new_buffer (adj_size)))) {
                lock.wait(cond); // wait until more memory is freed
            }

            if (0 != adj_buff) {
#ifndef NDEBUG
                bool fail = false;
                if (adj_ptr != adj_buff) {
                    log_fatal << "Assertion (adj_ptr == adj_buff) failed: "
                              << adj_ptr << " != " << adj_buff;
                    fail = true;
                }
                if (adj_ptr + adj_size != next) {
                    log_fatal << "Assertion (adj_ptr + adj_size == next) "
                              << "failed: " << (adj_ptr + adj_size) << " != "
                              << next;
                    fail = true;
                }
                if (fail) abort();
#endif
                cond.signal();
                // allocated adjacent space, buffer pointer not changed
                return ptr;
            }
        }

        // find non-adjacent buffer
        void* ptr_new = this->malloc (size);
        if (ptr_new != 0) {
            memcpy (ptr_new, ptr, bh->size - sizeof(BufferHeader));
            this->free (ptr);
        }

        return ptr_new;
    } 
}
