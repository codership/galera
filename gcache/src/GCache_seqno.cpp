/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#include "SeqnoNone.hpp"
#include "gcache_bh.hpp"
#include "GCache.hpp"

#include <galerautils.hpp>

#include <cerrno>
#include <cassert>

#include <sched.h> // sched_yeild()

namespace gcache
{
    /*!
     * Reinitialize seqno sequence (after SST or such)
     * Clears seqno->ptr map // and sets seqno_min to seqno.
     */
    void
    GCache::seqno_reset ()
    {
        gu::Lock lock(mtx);

        if (gu_unlikely(seqno2ptr.empty())) return;

        /* order is significant here */
        rb.seqno_reset();
        mem.seqno_reset();

        seqno2ptr.clear();

        seqno_released = 0;
    }

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void
    GCache::seqno_assign (const void* const ptr,
                          int64_t     const seqno_g,
                          int64_t     const seqno_d)
    {
        gu::Lock lock(mtx);

        BufferHeader* bh = ptr2BH(ptr);

        assert (SEQNO_NONE == bh->seqno_g);
        assert (SEQNO_ILL  == bh->seqno_d);
        assert (!BH_is_released(bh));

        if (gu_likely(seqno_g > seqno_max))
        {
            seqno2ptr.insert (seqno2ptr.end(), seqno2ptr_pair_t(seqno_g, ptr));
            seqno_max = seqno_g;
        }
        else
        {
            // this should never happen. seqnos should be assinged in TO.
            const std::pair<seqno2ptr_iter_t, bool>& res(
                seqno2ptr.insert (seqno2ptr_pair_t(seqno_g, ptr)));

            if (false == res.second)
            {
                gu_throw_fatal <<"Attempt to reuse the same seqno: " << seqno_g
                               <<". New ptr = " << ptr << ", previous ptr = "
                               << res.first->second;
            }
        }

        bh->seqno_g = seqno_g;
        bh->seqno_d = seqno_d;
    }

    void
    GCache::seqno_release (int64_t const seqno)
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

        bool   loop(false);

        do
        {
            /* if we're doing this loop repeatedly, allow other threads to run*/
            if (loop) sched_yield();

            gu::Lock lock(mtx);

            assert(seqno >= seqno_released);

            seqno2ptr_iter_t it(seqno2ptr.upper_bound(seqno_released));

            if (gu_unlikely(it == seqno2ptr.end()))
            {
                /* this means that there are no element with
                 * seqno following seqno_released - and this should never happen */
// this kills some other unit tests: assert(0);
                if (0 != seqno_released)
                {
                    log_warn << "Releasing seqno " << seqno << " before "
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

            int64_t const start(it->first - 1);
            int64_t const end  (seqno - start >= 2*batch_size ?
                                start + batch_size : seqno);
#if 0
            log_info << "############ releasing " << (seqno - start)
                     << " buffers, batch_size: " << batch_size
                     << ", end: " << end;
#endif
            for (;(loop = (it != seqno2ptr.end())) && it->first <= end;)
            {
                assert (seqno_released + 1 == it->first || seqno_released == 0);
                BufferHeader* const bh(ptr2BH(it->second));
                assert (bh->seqno_g == it->first);
                seqno_released = it->first;
                ++it; /* free_common() below may erase current element,
                       * so advance iterator before calling free_common() */
                if (gu_likely(!BH_is_released(bh))) free_common(bh);
            }

            assert (loop || seqno == seqno_released);

            loop = (end < seqno) && loop;
        }
        while(loop);
    }

    /*!
     * Move lock to a given seqno. Throw gu::NotFound if seqno is not in cache.
     * @throws NotFound
     */
    void GCache::seqno_lock (int64_t const seqno_g)
    {
        gu::Lock lock(mtx);

        if (seqno2ptr.find(seqno_g) == seqno2ptr.end()) throw gu::NotFound();

        if (seqno_locked != SEQNO_NONE)
        {
            cond.signal();
        }
        seqno_locked = seqno_g;
    }

    /*!
     * Get pointer to buffer identified by seqno.
     * Moves lock to the given seqno.
     * @throws NotFound
     */
    const void* GCache::seqno_get_ptr (int64_t const seqno_g,
                                       int64_t&      seqno_d,
                                       ssize_t&      size)
    {
        const void* ptr(0);

        {
            gu::Lock lock(mtx);

            seqno2ptr_iter_t p = seqno2ptr.find(seqno_g);

            if (p != seqno2ptr.end())
            {
                if (seqno_locked != SEQNO_NONE)
                {
                    cond.signal();
                }
                seqno_locked = seqno_g;

                ptr = p->second;
            }
            else
            {
                throw gu::NotFound();
            }
        }

        assert (ptr);

        const BufferHeader* const bh (ptr2BH(ptr)); // this can result in IO
        seqno_d = bh->seqno_d;
        size    = bh->size - sizeof(BufferHeader);

        return ptr;
    }

    ssize_t
    GCache::seqno_get_buffers (std::vector<Buffer>& v,
                               int64_t const start)
    {
        ssize_t const max(v.size());

        assert (max > 0);

        ssize_t found(0);

        {
            gu::Lock lock(mtx);

            seqno2ptr_iter_t p = seqno2ptr.find(start);

            if (p != seqno2ptr.end())
            {
                if (seqno_locked != SEQNO_NONE)
                {
                    cond.signal();
                }

                seqno_locked = start;

                do {
                    assert (p->first == (start + found));
                    assert (p->second);
                    v[found].set_ptr(p->second);
                }
                while (++found < max && ++p != seqno2ptr.end() &&
                       p->first == (start + found));
                /* the latter condition ensures seqno continuty, #643 */
            }
        }

        // the following may cause IO
        for (ssize_t i(0); i < found; ++i)
        {
            const BufferHeader* const bh (ptr2BH(v[i].ptr()));

            assert (bh->seqno_g == (start + i));

            v[i].set_other (bh->size - sizeof(BufferHeader),
                            bh->seqno_g,
                            bh->seqno_d);
        }

        return found;
    }

    /*!
     * Releases any history locks present.
     */
    void GCache::seqno_unlock ()
    {
        gu::Lock lock(mtx);
        seqno_locked = SEQNO_NONE;
        cond.signal();
    }
}
