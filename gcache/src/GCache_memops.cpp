/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

#include <cassert>

namespace gcache
{
    void
    GCache::discard_seqno (int64_t seqno)
    {
        for (seqno2ptr_t::iterator i = seqno2ptr.begin();
             i != seqno2ptr.end() && i->first <= seqno;)
        {
            seqno2ptr_t::iterator j = i; ++i;
            BufferHeader* bh = ptr2BH (j->second);
            seqno2ptr.erase (j);
            bh->seqno = SEQNO_NONE;

            switch (bh->store)
            {
            case BUFFER_IN_MEM:
                if (gu_likely(BH_is_released(bh))) free (bh);
                break;
            case BUFFER_IN_RB:
                if (gu_likely(BH_is_released(bh))) rb.discard_buffer (bh);
                break;
            case BUFFER_IN_PAGE:
                break;
            }
        }
    }

    void* 
    GCache::malloc (ssize_t size) throw (gu::Exception)
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
    GCache::free (void* ptr) throw ()
    {
        if (gu_likely(0 != ptr))
        {
            BufferHeader* bh = ptr2BH(ptr);
            gu::Lock      lock(mtx);

#ifndef NDEBUG
            std::set<const void*>::iterator it = buf_tracker.find(ptr);
            if (it == buf_tracker.end())
            {
                log_fatal << "Have not allocated this ptr: " << ptr;
                abort();
            }
            buf_tracker.erase(it);
#endif

            switch (bh->store)
            {
            case BUFFER_IN_MEM:  mem.free (ptr); break;
            case BUFFER_IN_RB:   rb.free  (ptr); break;
            case BUFFER_IN_PAGE:
                if (gu_likely(SEQNO_NONE != bh->seqno))
                {
                    discard_seqno (bh->seqno);
                }
                ps.free (ptr); break;
            }
        }
    }

    // this will crash if ptr == 0
    void*
    GCache::realloc (void* ptr, ssize_t size) throw (gu::Exception)
    {
        size += sizeof(BufferHeader);

        void*         new_ptr = 0;
        BufferHeader* bh      = ptr2BH(ptr);

        if (gu_unlikely(bh->seqno != SEQNO_NONE)) // sanity check
        {
            log_fatal << "Internal program error: changing size of an ordered"
                      << " buffer, seqno: " << bh->seqno << ". Aborting.";
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
                store->free (ptr);
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
