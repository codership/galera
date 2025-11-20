/*
 * Copyright (C) 2009-2025 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"
#include <cassert>
#include "gu_logger.hpp"

namespace gcache
{
    void
    GCache::discard_buffer(BufferHeader* bh, const void* ptr)
    {
        assert(bh->seqno_g > 0);

        switch (bh->store)
        {
        case BUFFER_IN_MEM:  mem.discard (bh); break;
        case BUFFER_IN_RB:   rb.discard  (bh); break;
        case BUFFER_IN_PAGE: ps.discard  (bh, ptr); break;
        default:
            log_fatal << "Corrupt buffer header: " << bh;
            abort();
        }
    }

    template <typename T> bool
    GCache::discard (T& cond)
    {
        assert(mtx.locked() && mtx.owned());

        bool const params_debug(params.debug());

#ifndef NDEBUG
        if (params_debug) cond.debug_begin();
#endif
        while (!seqno2ptr.empty() && cond.check())
        {
            if (seqno2ptr.index_begin() >= seqno_locked)
            {
                if (params_debug) cond.debug_locked(seqno_locked);
                return false;
            }

            const void* const ptr(seqno2ptr.front());
            BufferHeader* const bh(get_BH(ptr));

            if (gu_likely(BH_is_released(bh)))
            {
                assert (bh->seqno_g == seqno2ptr.index_begin());

                cond.update(bh);
                discard_buffer(bh, ptr);
            }
            else
            {
#ifndef NDEBUG
                if (params_debug) cond.debug_not_released();
#endif
                assert(cond.check());
                return false;
            }

            seqno2ptr.pop_front();
        }

        return true;
    }

    class DiscardSizeCond
    {
        size_t const upto_;
        size_t       done_;
    public:
        DiscardSizeCond(size_t s) : upto_(s), done_(0) {}
        bool check() const { return done_ < upto_; }
        void update(const BufferHeader* bh) { done_ += bh->size; }
        /* bh->size is actually a conservative freed estimate due to
         * store buffer alignment, which is different for each store
         * type. However it is not necessary to be exact here. Were are
         * just trying to discard some buffers because there are too many
         * allocated */
        void debug_begin()
        {
            log_info << "GCache::discard_size(" << upto_ << ")";
        }
        void debug_locked(seqno_t const locked)
        {
            log_info << "GCache::discard_size(): "
                     << locked << " is locked, bailing out.";
        }
        void debug_not_released()
        {
            log_info << "GCache::discard_size() can't discard "
                     << (upto_ - done_) << " bytes: not released, bailing out.";
        }
    };

    bool
    GCache::discard_size(size_t const size)
    {
        DiscardSizeCond cond(size);
        return discard<>(cond);
    }

    class DiscardSeqnoCond
    {
        seqno_t const upto_;
        seqno_t       done_;
    public:
        DiscardSeqnoCond(seqno_t start, seqno_t end)
            : upto_(end), done_(start - 1) {}
        bool check() const { return done_ < upto_; }
        void update(const BufferHeader* bh)
        {
            assert(done_ + 1 == bh->seqno_g);
            done_ = bh->seqno_g;
            }
        void debug_begin()
        {
            log_info << "GCache::discard_seqno(" << done_ + 1 << " - "
                     << upto_ << ")";
        }
        void debug_locked(seqno_t const locked)
        {
            log_info << "GCache::discard_seqno(" << upto_ << "): "
                     << locked << " is locked, bailing out.";
        }
        void debug_not_released()
        {
            log_info << "GCache::discard_seqno(" << upto_ << "): "
                     << done_ + 1 << " not released, bailing out.";
        }
    };

    bool
    GCache::discard_seqno (seqno_t seqno)
    {

        seqno_t const start(seqno2ptr.empty() ?
                            SEQNO_NONE : seqno2ptr.index_begin());
        assert(start > 0);

        DiscardSeqnoCond cond(start, seqno);

        return discard<>(cond);
    }

    void
    GCache::discard_tail (seqno_t const seqno)
    {
        while (seqno2ptr.index_back() > seqno && !seqno2ptr.empty())
        {
            const void* const ptr(seqno2ptr.back());
            BufferHeader* const bh(get_BH(ptr));

            assert(BH_is_released(bh));
            assert(bh->seqno_g == seqno2ptr.index_back());

            seqno2ptr.pop_back();
            discard_buffer(bh, ptr);
        }
    }

    void*
    GCache::malloc (ssize_type const s, void*& ptx)
    {
        assert(s >= 0);

        void* ptr(NULL);

        if (gu_likely(s > 0))
        {
            size_type const size(BH_size(s));

            gu::Lock lock(mtx);

            bool const page_cleanup(ps.page_cleanup_needed());
            /* try to discard twice as much as being allocated in order to
             * eventually delete some pages */
            if (page_cleanup) discard_size(2*size);

            mallocs++;

            if (!encrypt_cache)
            {
                ptr = mem.malloc(size);

                if (NULL == ptr)
                {
                    ptr = rb.malloc(size);
                    if (NULL == ptr) ptr = ps.malloc(size, ptx);
                }

                ptx = ptr;
            }
            else /* only page store can be used */
            {
                ptr = ps.malloc(size, ptx);
            }

#ifndef NDEBUG
            if (0 != ptr) buf_tracker.insert (ptr);
#endif
        }
        else ptx = NULL;

        assert((uintptr_t(ptr) % MemOps::ALIGNMENT) == 0);

        return ptr;
    }

    void
    GCache::free_common (BufferHeader* const bh, const void* const ptr)
    {
        assert(bh->seqno_g != SEQNO_ILL);
        BH_release(bh);

        if (gu_likely(SEQNO_NONE != bh->seqno_g))
        {
#ifndef NDEBUG
            if (!(seqno_released + 1 == bh->seqno_g ||
                  SEQNO_NONE == seqno_released))
            {
                log_fatal << "OOO release: seqno_released " << seqno_released
                          << ", releasing " << bh->seqno_g;
                assert(0);
            }
#endif
            seqno_released = bh->seqno_g;
        }
#ifndef NDEBUG
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
        case BUFFER_IN_PAGE: ps.free  (bh, ptr); break;
        default:
            log_fatal << "Memory corruption: unrecognized store: " << bh->store;
            abort();
        }

        rb.assert_size_free();
    }

    void
    GCache::free (void* ptr)
    {
        if (gu_likely(0 != ptr))
        {
            try
            {
                gu::Lock lock(mtx);
                BufferHeader* const bh(get_BH(ptr));
                /* free() should not be used on ordered buffers,
                 * GCache::seqno_release() should be used instead */
                assert(bh->seqno_g <= 0);

#ifndef NDEBUG
                assert(bh->store == BUFFER_IN_PAGE || !encrypt_cache);
                if (params.debug()) { log_info << "GCache::free() " << bh; }
                seqno_t const old_sr(seqno_released);
#endif
            free_common (bh, ptr);
#ifndef NDEBUG
                if (params.debug())
                {
                    log_info << "GCache::free() seqno_released: "
                             << old_sr << " -> " << seqno_released;
                }
#endif
            }
            catch(gu::Exception& e)
            {
                gu_error("GCache::free() caught exception %s.", e.what());
                gu_abort();
            }
        }
        else {
            log_warn << "Attempt to free a null pointer";
            assert(0);
        }
    }

    void*
    GCache::realloc (void* const ptr, ssize_type const s, void*& ptx)
    {
        assert(s >= 0);

        if (NULL == ptr)
        {
            return malloc(s, ptx);
        }
        else if (s == 0)
        {
            free (ptr);
            ptx = NULL;
            return NULL;
        }

        assert((uintptr_t(ptr) % MemOps::ALIGNMENT) == 0);

        BufferHeader* const bh(get_BH(ptr));

        if (gu_unlikely(bh->seqno_g > 0)) // sanity check
        {
            log_fatal << "Internal program error: changing size of an "
                "ordered buffer, seqno: " << bh->seqno_g << ". Aborting.";
            abort();
        }

        size_type const size(BH_size(s));

        MemOps* store(NULL);
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
        assert(store);

        void*    new_ptr(NULL);

        reallocs++;

        if (!encrypt_cache)
        {
            /* with non-encrypted cache we may try in-store realloc() */
            gu::Lock lock(mtx);
            new_ptr = store->realloc(ptr, size);
            ptx = new_ptr;
        }
        else
        {
            assert(&ps == store);
        }

        if (NULL == new_ptr)
        {
            /* if in-store realloc() failed or cache is encrypted, we need
             * to resort to malloc() + memcpy() + free() */
            new_ptr = malloc(size, ptx);

            if (NULL != new_ptr)
            {
                assert(NULL != ptx);
                /* bh points to old PLAINTEXT, ptx - to new */
                ::memcpy(ptx, bh + 1, bh->size - sizeof(BufferHeader));
                gu::Lock lock(mtx);
                store->free(bh);
            }
            else
            {
                assert(NULL == ptx);
            }
        }

#ifndef NDEBUG
        if (ptr != new_ptr && NULL != new_ptr)
        {
            gu::Lock lock(mtx);
            std::set<const void*>::iterator it = buf_tracker.find(ptr);

            if (it != buf_tracker.end()) buf_tracker.erase(it);

            it = buf_tracker.find(new_ptr);

        }
#endif
        assert((uintptr_t(new_ptr) % MemOps::ALIGNMENT) == 0);

        return new_ptr;
    }
}
