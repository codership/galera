/*
 * Copyright (C) 2009-2011 Codership Oy <info@codership.com>
 */

#include <cerrno>
#include <cassert>

#include <galerautils.hpp>
#include "SeqnoNone.hpp"
#include "gcache_bh.hpp"
#include "GCache.hpp"

namespace gcache
{
    /*!
     * Reinitialize seqno sequence (after SST or such)
     * Clears seqno->ptr map // and sets seqno_min to seqno.
     */
#if OLD
    void
    GCache::seqno_reset (/*int64_t seqno*/)
    {
        gu::Lock lock(mtx);

        if (!seqno2ptr.empty())
        {
            int64_t old_min = seqno2ptr.begin()->first;
            int64_t old_max = seqno2ptr.rbegin()->first;

            log_info << "Discarding old history seqnos from cache: " << old_min
                     << '-' << old_max;

            discard_seqno (old_max); // forget all previous seqnos
        }

//        seqno_min = seqno;
    }
#else
    void
    GCache::seqno_reset ()
    {
        gu::Lock lock(mtx);

        if (seqno2ptr.empty()) return;

        /* order is significant here */
        rb.seqno_reset();
        mem.seqno_reset();

        seqno2ptr.clear();
    }
#endif

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void
    GCache::seqno_assign (const void* const ptr,
                          int64_t     const seqno_g,
                          int64_t     const seqno_d,
                          bool        const free)
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
        if (free) free_common(bh);
    }
#if DEPRECATED
    /*!
     * Get the smallest seqno present in the cache.
     * Locks seqno from removal.
     */
    int64_t GCache::seqno_get_min ()
    {
        gu::Lock lock(mtx);

        // This is a protection against concurrent history locking.
        // I don't envision the need for concurrent history access, so I don't
        // implement anything fancier.
        while (seqno_locked != SEQNO_NONE) lock.wait(cond);

        if (!seqno2ptr.empty()) {
            seqno_locked = seqno2ptr.begin()->first;
            return seqno_locked;
        }

        return SEQNO_NONE;
    }
#endif

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
    void GCache::seqno_release ()
    {
        gu::Lock lock(mtx);
        seqno_locked = SEQNO_NONE;
        cond.signal();
    }
}
