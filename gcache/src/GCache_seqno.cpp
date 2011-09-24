/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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
     * Clears cache and sets seqno_min to seqno.
     */
    void
    GCache::seqno_init (int64_t seqno)
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

        seqno_min = seqno;
    }

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void
    GCache::seqno_assign (const void* const ptr,
                          int64_t     const seqno_g,
                          int64_t     const seqno_d,
                          bool        const release)
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
        if (release) BH_release(bh);
    }

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

    /*!
     * Get pointer to buffer identified by seqno.
     * Moves lock to the given seqno.
     */
    const void* GCache::seqno_get_ptr (int64_t const seqno_g,int64_t& seqno_d)
    {
        gu::Lock lock(mtx);

        if (seqno_g >= seqno_locked) {
            seqno2ptr_iter_t p = seqno2ptr.find(seqno_g);
            if (p != seqno2ptr.end())
            {
                if (seqno_locked != seqno_g)
                {
                    seqno_locked = seqno_g;
                    cond.signal();
                }

                const void* const ptr  = p->second;
                BufferHeader* const bh = ptr2BH(ptr);
                seqno_d = bh->seqno_d;
                return ptr;
            }
        }
        else {
            gu_throw_fatal << "Attempt to acquire buffer by seqno out of order:"
                           << " locked seqno: " << seqno_locked
                           << ", requested seqno: " << seqno_g;
        }

        return 0;
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
