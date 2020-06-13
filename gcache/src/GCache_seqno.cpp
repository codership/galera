/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "gcache_bh.hpp"
#include "GCache.hpp"

#include <cerrno>
#include <cassert>

#include <sched.h> // sched_yeild()

namespace gcache
{
    /*!
     * Reinitialize seqno sequence (after SST or such)
     * Clears seqno->ptr map // and sets seqno_min to gtid seqno
     */
    void
    GCache::seqno_reset (const gu::GTID& gtid)
    {
        gu::Lock lock(mtx);

        assert(seqno2ptr.empty() || seqno_max == seqno2ptr.index_back());

        const seqno_t s(gtid.seqno());
        if (gtid.uuid() == gid && s != SEQNO_ILL && seqno_max >= s)
        {
            if (seqno_max > s)
            {
                discard_tail(s);
                seqno_max = s;
                seqno_released = s;
                assert(seqno_max == seqno2ptr.index_back());
            }
            return;
        }

        log_info << "GCache history reset: " << gu::GTID(gid, seqno_max)
                 << " -> " << gtid;

        seqno_released = SEQNO_NONE;
        gid = gtid.uuid();

        /* order is significant here */
        rb.seqno_reset();
        mem.seqno_reset();

        seqno2ptr.clear(SEQNO_NONE);
        seqno_max = SEQNO_NONE;
    }

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void
    GCache::seqno_assign (const void* const ptr,
                          seqno_t     const seqno_g,
                          uint8_t     const type,
                          bool        const skip)
    {
        gu::Lock lock(mtx);

        BufferHeader* bh = ptr2BH(ptr);

        assert (SEQNO_NONE == bh->seqno_g);
        assert (seqno_g > 0);
        assert (!BH_is_released(bh));

        if (gu_likely(seqno_g > seqno_max))
        {
            seqno_max = seqno_g;
        }
        else
        {
            seqno2ptr_iter_t const i(seqno2ptr.find(seqno_g));

            if (i != seqno2ptr.end())
            {
                const void* const prev_ptr(*i);

                if (!seqno2ptr_t::not_set(prev_ptr))
                {
                    const BufferHeader* const prev_bh(ptr2BH(prev_ptr));
                    assert(0);
                    gu_throw_fatal << "Attempt to reuse the same seqno: "
                                   << seqno_g <<". New buffer: " << bh
                                   << ", previous buffer: " << prev_bh;
                }
            }

            seqno_released = std::min(seqno_released, seqno_g - 1);
        }

        seqno2ptr.insert(seqno_g, ptr);

        bh->seqno_g = seqno_g;
        bh->flags  |= (BUFFER_SKIPPED * skip);
        bh->type    = type;
    }

    /*!
     * Mark buffer to be skipped
     */
    void
    GCache::seqno_skip (const void* const ptr,
                        seqno_t     const seqno_g,
                        uint8_t     const type)
    {
        gu::Lock lock(mtx);

        BufferHeader* const bh(ptr2BH(ptr));
        seqno2ptr_iter_t p = seqno2ptr.find(seqno_g);

        /* sanity checks */
        int reason(0);
        std::ostringstream msg;

        if (seqno_g <= 0) {
            msg << "invalid seqno: " << seqno_g;
            reason = 1;
        }
        else if (seqno_g != bh->seqno_g) {
            msg << "seqno " << seqno_g << " does not match ptr seqno "
                << bh->seqno_g;
            reason = 2;
        }
        else if (type != bh->type) {
            msg << "type " << type << " does not match ptr type "
                << bh->type;
            reason = 3;
        }
        else if (p == seqno2ptr.end()) {
            msg << "seqno " << seqno_g << " not found in the map";
            reason = 4;
        }
        else if (ptr != *p) {
            msg << "ptr " << seqno_g << " does not match mapped ptr "
                << *p;
            reason = 5;
        }

        assert(0 == reason);
        if (0 != reason)
        {
            gu_throw_fatal << "Skipping seqno sanity check failed: " << msg.str()
                           << " (reason " << reason << ")";
        }

        assert (!BH_is_released(bh));
        assert (!BH_is_skipped(bh));

        bh->flags |= BUFFER_SKIPPED;
    }

    void
    GCache::seqno_release (seqno_t const seqno)
    {
        assert (seqno > 0);
        /* The number of buffers scheduled for release is unpredictable, so
         * we want to allow some concurrency in cache access by releasing
         * buffers in small batches */
        static int const min_batch_size(32);

        /* Although extremely unlikely, theoretically concurrent access may
         * lead to elements being added faster than released. The following is
         * to control and possibly disable concurrency in that case. We start
         * with min_batch_size and increase it if necessary. */
        size_t old_gap(-1);
        int    batch_size(min_batch_size);

        bool   loop(seqno >= seqno_released);

        while(loop)
        {
            gu::Lock lock(mtx);

            assert(seqno >= seqno_released);

            seqno_t idx(seqno2ptr.upper_bound(seqno_released));

            if (gu_unlikely(idx == seqno2ptr.index_end()))
            {
                /* This means that there are no elements with
                 * seqno following seqno_released - and this should not
                 * generally happen. But it looks like stopcont test does it. */
                if (SEQNO_NONE != seqno_released)
                {
                    log_debug << "Releasing seqno " << seqno << " before "
                              << seqno_released + 1 << " was assigned.";
                }
                return;
            }

            assert(seqno_max >= seqno_released);

            /* here we check if (seqno_max - seqno_released) is decreasing
             * and if not - increase the batch_size (linearly) */
            size_t const new_gap(seqno_max - seqno_released);
            batch_size += (new_gap >= old_gap) * min_batch_size;
            old_gap = new_gap;

            seqno_t const start(idx - 1);
            seqno_t const end  (seqno - start >= 2*batch_size ?
                                start + batch_size : seqno);
#ifndef NDEBUG
            if (params.debug())
            {
                log_info << "GCache::seqno_release(" << seqno << "): "
                         << (seqno - start) << " buffers, batch_size: "
                         << batch_size << ", end: " << end;
            }
#endif
            while((loop = (idx < seqno2ptr.index_end())) && idx <= end)
            {
                assert(idx != SEQNO_NONE);
                BufferHeader* const bh(ptr2BH(seqno2ptr[idx]));
                assert (bh->seqno_g == idx);
#ifndef NDEBUG
                if (!(seqno_released + 1 == idx ||
                      seqno_released == SEQNO_NONE))
                {
                    log_info << "seqno_released: " << seqno_released
                             << "; idx: " << idx
                             << "; seqno2ptr.begin: " <<seqno2ptr.index_begin()
                             << "\nstart: " << start << "; end: " << end
                             << " batch_size: " << batch_size << "; gap: "
                             << new_gap << "; seqno_max: " << seqno_max;
                    assert(seqno_released + 1 == idx ||
                           seqno_released == SEQNO_NONE);
                }
#endif
                if (gu_likely(!BH_is_released(bh))) free_common(bh);
                /* free_common() could modify a map, look for next unreleased
                 * seqno. */
                idx = seqno2ptr.upper_bound(idx);
            }

            assert (loop || seqno == seqno_released);

            loop = (end < seqno) && loop;

            /* if we're doing this loop repeatedly, allow other threads to run*/
            if (loop) sched_yield();
        }
    }

    /*!
     * Move lock to a given seqno. Throw gu::NotFound if seqno is not in cache.
     * @throws NotFound
     */
    void GCache::seqno_lock (seqno_t const seqno_g)
    {
        gu::Lock lock(mtx);

        assert(seqno_g > 0);

        assert(SEQNO_MAX == seqno_locked || seqno_locked_count > 0);
        assert(0   == seqno_locked_count || seqno_locked < SEQNO_MAX);

        seqno2ptr.at(seqno_g); /* check that the element exists */

        seqno_locked_count++;

        if (seqno_g < seqno_locked) seqno_locked = seqno_g;
    }

    /*!
     * Get pointer to buffer identified by seqno.
     * Repossesses the buffer if it was already released, preventing its
     * (and following buffers) discarding.
     * @throws NotFound
     */
    const void* GCache::seqno_get_ptr (seqno_t const seqno_g,
                                       ssize_t&      size)
    {
        gu::Lock lock(mtx);

        const void* const ptr(seqno2ptr.at(seqno_g));
        assert (ptr);

        BufferHeader* const bh(ptr2BH(ptr));
        assert(seqno_g == bh->seqno_g);

        if (BH_is_released(bh)) // repossess and revert the effects of free()
        {
#ifndef NDEBUG
            buf_tracker.insert(ptr);
#endif
            seqno_released = std::min(seqno_released, bh->seqno_g - 1);
            mallocs++; // to match the resulting frees count

            // notify store
            switch (bh->store)
            {
            case BUFFER_IN_MEM:  mem.repossess(bh); break;
            case BUFFER_IN_RB:   rb.repossess (bh); break;
            case BUFFER_IN_PAGE: assert(0); break; // for the moment buffers
                                                   // do not linger in pages
            default: assert(0);
            }

            bh->flags &= ~BUFFER_RELEASED; // clear released flag
        }

        size = bh->size - sizeof(BufferHeader);

        return ptr;
    }

    size_t
    GCache::seqno_get_buffers (std::vector<Buffer>& v,
                               seqno_t const start)
    {
        size_t const max(v.size());

        assert (max > 0);

        size_t found(0);

        {
            gu::Lock lock(mtx);

            assert(seqno_locked <= start);
            // the caller should have locked the range first

            seqno2ptr_iter_t p = seqno2ptr.find(start);

            if (p != seqno2ptr.end() && *p)
            {
                do {
                    assert(seqno2ptr.index(p) == seqno_t(start + found));
                    assert(*p);
                    v[found].set_ptr(*p);
                }
                while (++found < max && ++p != seqno2ptr.end() && *p);
                /* the last condition ensures seqno continuty, #643 */
            }
        }

        // the following may cause IO
        for (size_t i(0); i < found; ++i)
        {
            const BufferHeader* const bh (ptr2BH(v[i].ptr()));

            assert (bh->seqno_g == seqno_t(start + i));
            Limits::assert_size(bh->size);

            v[i].set_other (bh->seqno_g,
                            bh->size - sizeof(BufferHeader),
                            BH_is_skipped(bh),
                            bh->type);
        }

        return found;
    }

    /*!
     * Releases any history locks present.
     */
    void GCache::seqno_unlock ()
    {
        gu::Lock lock(mtx);

        if (seqno_locked_count > 0)
        {
            assert(seqno_locked < SEQNO_MAX);
            seqno_locked_count--;
            if (0 == seqno_locked_count) seqno_locked = SEQNO_MAX;
        }
        else
        {
            assert(SEQNO_MAX == seqno_locked);
            assert(0); // something wrong with the caller's logic
            seqno_locked = SEQNO_MAX;
        }
    }
}
