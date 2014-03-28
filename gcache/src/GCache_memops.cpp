/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

#include <cassert>

namespace gcache
{
    bool
    GCache::discard_seqno (int64_t seqno)
    {
//        seqno = std::min(seqno, seqno_released);
        for (seqno2ptr_t::iterator i = seqno2ptr.begin();
             i != seqno2ptr.end() && i->first <= seqno;)
        {
            seqno2ptr_t::iterator j(i); ++i;
            BufferHeader* bh(ptr2BH (j->second));

            if (gu_likely(BH_is_released(bh)))
            {
                assert (bh->seqno_g <= seqno);

                seqno2ptr.erase (j);
                bh->seqno_g = SEQNO_ILL; // will never be reused

                switch (bh->store)
                {
                case BUFFER_IN_MEM:  mem.discard (bh); break;
                case BUFFER_IN_RB:   rb.discard  (bh); break;
                case BUFFER_IN_PAGE: ps.discard  (bh); break;
                default:
                    log_fatal << "Corrupt buffer header: " << bh;
                    abort();
                }
            }
            else
            {
                return false;
            }
        }

        return true;
    }

    void*
    GCache::malloc (ssize_t size)
    {
        size += sizeof(BufferHeader);

        gu::Lock lock(mtx);
        void*    ptr;

        mallocs++;

        ptr = mem.malloc(size);

        if (0 == ptr) ptr = rb.malloc(size);

        if (0 == ptr) ptr = ps.malloc(size);

#ifndef NDEBUG
        if (0 != ptr) buf_tracker.insert (ptr);
#endif
        return ptr;
    }

    void
    GCache::free_common (BufferHeader* const bh)
    {
        assert(bh->seqno_g != SEQNO_ILL);
        BH_release(bh);

#ifndef NDEBUG
        void* const ptr(bh + 1);
        std::set<const void*>::iterator it = buf_tracker.find(ptr);
        if (it == buf_tracker.end())
        {
            log_fatal << "Have not allocated this ptr: " << ptr;
            abort();
        }
        buf_tracker.erase(it);
#endif
        frees++;

        switch (bh->store)
        {
        case BUFFER_IN_MEM:  mem.free (bh); break;
        case BUFFER_IN_RB:   rb.free  (bh); break;
        case BUFFER_IN_PAGE:
            if (gu_likely(bh->seqno_g > 0))
            {
                discard_seqno (bh->seqno_g);
            }
            else
            {
                assert(bh->seqno_g != SEQNO_ILL);
                bh->seqno_g = SEQNO_ILL;
                ps.discard (bh);
            }
            break;
        }
        rb.assert_size_free();
    }

    void
    GCache::free (void* ptr)
    {
        if (gu_likely(0 != ptr))
        {
            BufferHeader* const bh(ptr2BH(ptr));
            gu::Lock      lock(mtx);

            free_common (bh);
        }
        else {
            log_warn << "Attempt to free a null pointer";
            assert(0);
        }
    }

    // this will crash if ptr == 0
    void*
    GCache::realloc (void* ptr, ssize_t size)
    {
        size += sizeof(BufferHeader);

        void*               new_ptr(0);
        BufferHeader* const bh(ptr2BH(ptr));

        if (gu_unlikely(bh->seqno_g > 0)) // sanity check
        {
            log_fatal << "Internal program error: changing size of an ordered"
                      << " buffer, seqno: " << bh->seqno_g << ". Aborting.";
            abort();
        }

        gu::Lock      lock(mtx);

        reallocs++;

        MemOps* store(0);

        switch (bh->store)
        {
        case BUFFER_IN_MEM:  store = &mem; break;
        case BUFFER_IN_RB:   store = &rb;  break;
        case BUFFER_IN_PAGE: store = &ps;  break;
        default:
            log_fatal << "Memory corruption: unrecognized store: "
                      << bh->store;
            abort();
        }

        new_ptr = store->realloc (ptr, size);

        if (0 == new_ptr)
        {
            new_ptr = malloc (size);

            if (0 != new_ptr)
            {
                memcpy (new_ptr, ptr, bh->size - sizeof(BufferHeader));
                store->free (bh);
            }
        }

#ifndef NDEBUG
        if (ptr != new_ptr && 0 != new_ptr)
        {
            std::set<const void*>::iterator it = buf_tracker.find(ptr);

            if (it != buf_tracker.end()) buf_tracker.erase(it);

            it = buf_tracker.find(new_ptr);

        }
#endif

        return new_ptr;
    }
}
