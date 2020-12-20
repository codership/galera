/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

#include <cassert>

namespace gcache
{
    void
    GCache::discard_buffer (BufferHeader* bh)
    {
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

    bool
    GCache::discard_seqno (seqno_t seqno)
    {
#ifndef NDEBUG
        seqno_t const begin(params.debug() ?
                            (seqno2ptr.empty() ?
                             seqno2ptr.index_begin() : SEQNO_NONE) : SEQNO_NONE);
        if (params.debug())
        {
            assert(begin > 0);
            log_info << "GCache::discard_seqno(" << begin << " - "
                     << seqno << ")";
        }
#endif
        /* if we can't complete the operation, let's not even start */
        if (seqno >= seqno_locked)
        {
#ifndef NDEBUG
            if (params.debug())
            {
                log_info << "GCache::discard_seqno(" << begin << " - " << seqno
                         << "): " << seqno_locked << " is locked, bailing out.";
            }
#endif
            return false;
        }

        while (seqno2ptr.index_begin() <= seqno && !seqno2ptr.empty())
        {
            BufferHeader* const bh(ptr2BH(seqno2ptr.front()));

            if (gu_likely(BH_is_released(bh)))
            {
                assert (bh->seqno_g == seqno2ptr.index_begin());
                assert (bh->seqno_g <= seqno);
                discard_buffer(bh);
            }
            else
            {
#ifndef NDEBUG
                if (params.debug())
                {
                    log_info << "GCache::discard_seqno(" << begin << " - "
                             << seqno << "): "
                             << bh->seqno_g << " not released, bailing out.";
                }
#endif
                return false;
            }

            seqno2ptr.pop_front();
        }

        return true;
    }

    void
    GCache::discard_tail (seqno_t const seqno)
    {
        while (seqno2ptr.index_back() > seqno && !seqno2ptr.empty())
        {
            BufferHeader* bh(ptr2BH(seqno2ptr.back()));

            assert(BH_is_released(bh));
            assert(bh->seqno_g == seqno2ptr.index_back());

            discard_buffer(bh);
            seqno2ptr.pop_back();
        }
    }

    void*
    GCache::malloc (ssize_type const s)
    {
        assert(s >= 0);

        void* ptr(NULL);

        if (gu_likely(s > 0))
        {
            size_type const size(MemOps::align_size(s + sizeof(BufferHeader)));

            gu::Lock lock(mtx);

            mallocs++;

            ptr = mem.malloc(size);

            if (0 == ptr) ptr = rb.malloc(size);

            if (0 == ptr) ptr = ps.malloc(size);

#ifndef NDEBUG
            if (0 != ptr) buf_tracker.insert (ptr);
#endif
        }

        assert((uintptr_t(ptr) % MemOps::ALIGNMENT) == 0);

        return ptr;
    }

    void
    GCache::free_common (BufferHeader* const bh)
    {
        assert(bh->seqno_g != SEQNO_ILL);
        BH_release(bh);

        seqno_t new_released(seqno_released);

        if (gu_likely(SEQNO_NONE != bh->seqno_g))
        {
#ifndef NDEBUG
            if (!(seqno_released + 1 == bh->seqno_g ||
                  SEQNO_NONE == seqno_released))
            {
                log_fatal << "OOO release: seqno_released " << seqno_released
                          << ", releasing " << bh->seqno_g;
            }
            assert(seqno_released + 1 == bh->seqno_g ||
                   SEQNO_NONE == seqno_released);
#endif
            new_released = bh->seqno_g;
        }
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
                if (gu_unlikely(!discard_seqno(bh->seqno_g)))
                {
                    new_released = (bh->seqno_g - 1);
                    assert(seqno_released <= new_released);
                }
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

#ifndef NDEBUG
        if (params.debug())
        {
            log_info << "GCache::free_common(): seqno_released: "
                     << seqno_released << " -> " << new_released;
        }
#endif
        seqno_released = new_released;
    }

    void
    GCache::free (void* ptr)
    {
        if (gu_likely(0 != ptr))
        {
            BufferHeader* const bh(ptr2BH(ptr));
            gu::Lock      lock(mtx);

#ifndef NDEBUG
            if (params.debug()) { log_info << "GCache::free() " << bh; }
#endif
            free_common (bh);
        }
        else {
            log_warn << "Attempt to free a null pointer";
            assert(0);
        }
    }

    void*
    GCache::realloc (void* const ptr, ssize_type const s)
    {
        assert(s >= 0);

        if (NULL == ptr)
        {
            return malloc(s);
        }
        else if (s == 0)
        {
            free (ptr);
            return NULL;
        }

        assert((uintptr_t(ptr) % MemOps::ALIGNMENT) == 0);

        size_type const size(MemOps::align_size(s + sizeof(BufferHeader)));

        void*               new_ptr(NULL);
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
        assert((uintptr_t(new_ptr) % MemOps::ALIGNMENT) == 0);

        return new_ptr;
    }
}
