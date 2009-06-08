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
    GCache::order_buffer (void* ptr, int64_t seqno)
    {
        BufferHeader* bh = BH(ptr);

        assert (bh->seqno = SEQNO_NONE);
        assert (!BH_is_released(bh));

        bh->seqno = seqno;
        seqno2ptr[seqno] = ptr;
        seqno_max = seqno;
    }

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void
    GCache::seqno_assign  (void* ptr, int64_t seqno)
    {
        gu::Lock lock(mtx);

        if (seqno_max != SEQNO_NONE && seqno == seqno_max + 1) {
            // normal case
            order_buffer (ptr, seqno);
        }
        else {
            if (SEQNO_NONE == seqno_max) {
                // bootstrapping the map
                assert (SEQNO_NONE == seqno_min);
                order_buffer (ptr, seqno);
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
            seqno2ptr_t::iterator b = seqno2ptr.begin();
            if (b->first != seqno_min) {
                log_fatal << "Expected smallest seqno: " << seqno_min
                          << ", but found: " << b->first;
                abort();
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
    void*   GCache::seqno_get_ptr (int64_t seqno)
    {
        gu::Lock lock(mtx);

        if (seqno >= seqno_locked) {
            seqno2ptr_t::iterator p = seqno2ptr.find(seqno);
            if (p != seqno2ptr.end()) {
                if (seqno_locked != seqno) {
                    seqno_locked = seqno;
                    cond.signal();
                }
                return p->second;
            }
        }
        else {
            std::ostringstream msg;
            msg << "Attempt to acquire buffer by seqno out of order: "
                << "locked seqno: " << seqno_locked << ", requested seqno: "
                << seqno;
            throw gu::Exception(msg.str().c_str(), ENOTRECOVERABLE);
        }

        return 0;
    }

    /*!
     * Releases any history locks present.
     */
    void    GCache::seqno_release ()
    {
        gu::Lock lock(mtx);
        seqno_locked = SEQNO_NONE;
        cond.signal();
    }
}
