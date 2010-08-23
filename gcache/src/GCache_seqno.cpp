/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include <cerrno>
#include <cassert>

#include <galerautils.hpp>
#include "SeqnoNone.hpp"
#include "BufferHeader.hpp"
#include "GCache.hpp"

namespace gcache
{
    inline void
    GCache::order_buffer (const void* ptr, int64_t seqno)
    {
        BufferHeader* bh = ptr2BH(ptr);

        assert (bh->seqno == SEQNO_NONE);
        assert (!BH_is_released(bh));

        bh->seqno = seqno;
        seqno2ptr[seqno] = ptr;
        seqno_max = seqno;
    }

    /*!
     * Reinitialize seqno sequence (after SST or such)
     * Clears cache and sets seqno_min to seqno.
     */
    void
    GCache::seqno_init (int64_t seqno)
    {
        gu::Lock lock(mtx);

        reset_cache();
        seqno_min = seqno;
    }

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void
    GCache::seqno_assign (const void* ptr, int64_t seqno)
    {
        gu::Lock lock(mtx);

#if 1
        BufferHeader* bh = ptr2BH(ptr);

        assert (SEQNO_NONE == bh->seqno);
        assert (!BH_is_released(bh));

        bh->seqno = seqno;

        if (gu_likely(seqno > seqno_max))
        {
            last_insert = seqno2ptr.insert (last_insert,
                                            seqno2ptr_pair(seqno, ptr));
            seqno_max = seqno > seqno_max ? seqno : seqno_max;
        }
        else
        {
            const std::pair<seqno2ptr_it, bool>& res(
                seqno2ptr.insert (seqno2ptr_pair(seqno, ptr)));

            if (false == res.second)
            {
                gu_throw_fatal << "Attempt to reuse the same seqno: " << seqno
                               <<". New ptr = " << ptr << ", previous ptr = "
                               << res.first->second;
            }
        }

#else // old stuff, delete
        if (seqno_max != SEQNO_NONE && seqno == seqno_max + 1) {
            // normal case
            order_buffer (const_cast<void*>(ptr), seqno);
        }
        else {
            if (SEQNO_NONE == seqno_max) {
                // bootstrapping the map
                assert (SEQNO_NONE == seqno_min);
                order_buffer (const_cast<void*>(ptr), seqno);
                seqno_min = seqno;
            }

            std::ostringstream msg;
            if (seqno <= seqno_max) {
                msg << "Attempt tp assign outdated seqno: " << seqno
                    << ", expected " << (seqno_max + 1);
                throw gu::Exception(msg.str().c_str(), ENOTRECOVERABLE);
            }
            else {
                msg << "Attempt to assign seqno out of order: " << seqno
                    << ", expected " << (seqno_max + 1);
                throw gu::Exception(msg.str().c_str(), EAGAIN);
            }
        }
#endif
    }

    /*!
     * Get the smallest seqno present in the cache.
     * Locks seqno from removal.
     */
    int64_t GCache::seqno_get_min ()
    {
        gu::Lock lock(mtx);

        // This is a concurrent protection against concurrent history locking.
        // I don't envision the need for concurrent history access, so I don't
        // implement anything fancier.
        while (seqno_locked != SEQNO_NONE) lock.wait(cond);

        if (!seqno2ptr.empty()) {
#ifndef NDEBUG
            seqno2ptr_it b = seqno2ptr.begin();
            if (b->first != seqno_min) {
                gu_throw_fatal << "Expected smallest seqno: " << seqno_min
                               << ", but found: " << b->first;
            }
#endif
            seqno_locked = seqno_min;
            return seqno_locked;
        }

        return SEQNO_NONE;
    }

    /*!
     * Get pointer to buffer identified by seqno.
     * Moves lock to the given seqno.
     */
    const void* GCache::seqno_get_ptr (int64_t seqno)
    {
        gu::Lock lock(mtx);

        if (seqno >= seqno_locked) {
            seqno2ptr_it p = seqno2ptr.find(seqno);
            if (p != seqno2ptr.end()) {
                if (seqno_locked != seqno) {
                    seqno_locked = seqno;
                    cond.signal();
                }
                return p->second;
            }
        }
        else {
            gu_throw_fatal << "Attempt to acquire buffer by seqno out of order:"
                           << " locked seqno: " << seqno_locked
                           << ", requested seqno: " << seqno;
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
