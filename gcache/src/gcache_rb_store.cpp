/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "gcache_rb_store.hpp"

#include <galerautils.hpp>
#include <cassert>

namespace gcache
{
    static size_t check_size (ssize_t s)
    {
        if (s < 0) gu_throw_error(EINVAL) << "Negative cache file size: " << s;

        return s + RingBuffer::pad_size() + sizeof(BufferHeader);
    }

    void
    RingBuffer::reset()
    {
        first_ = start_;
        next_  = start_;

        BH_clear (reinterpret_cast<BufferHeader*>(next_));

        size_free_ = size_cache_;
        size_used_ = 0;
        size_trail_= 0;

//        mallocs_  = 0;
//        reallocs_ = 0;
    }

    void
    RingBuffer::constructor_common() {}

    RingBuffer::RingBuffer (const std::string& name, ssize_t size,
                            std::map<int64_t, const void*> & seqno2ptr)
        throw (gu::Exception)
    :
        fd_        (name, check_size(size)),
        mmap_      (fd_),
        open_      (true),
        preamble_  (static_cast<char*>(mmap_.ptr)),
        header_    (reinterpret_cast<int64_t*>(preamble_ + PREAMBLE_LEN)),
        start_     (reinterpret_cast<uint8_t*>(header_   + HEADER_LEN)),
        end_       (reinterpret_cast<uint8_t*>(preamble_ + mmap_.size)),
        first_     (start_),
        next_      (first_),
        size_cache_(end_ - start_ - sizeof(BufferHeader)),
        size_free_ (size_cache_),
        size_used_ (0),
        size_trail_(0),
//        mallocs_   (0),
//        reallocs_  (0),
        seqno2ptr_ (seqno2ptr)
    {
        constructor_common ();
        BH_clear (reinterpret_cast<BufferHeader*>(next_));
    }

    RingBuffer::~RingBuffer ()
    {
        open_ = false;
        mmap_.sync();
        mmap_.unmap();
    }

    inline void
    RingBuffer::discard_seqno (int64_t seqno)
    {
        for (seqno2ptr_t::iterator i = seqno2ptr_.begin();
             i != seqno2ptr_.end() && i->first <= seqno;)
        {
            seqno2ptr_t::iterator j = i; ++i;
            BufferHeader* bh = ptr2BH (j->second);
            seqno2ptr_.erase (j);

            // this buffer will never ever be accessed by seqno again anyways
            bh->seqno_g = SEQNO_NONE;

            if (gu_likely (BH_is_released(bh)))
            {
                switch (bh->store)
                {
                case BUFFER_IN_MEM:
                    bh->ctx->discard(bh);
                    break;
                case BUFFER_IN_RB:
                    discard (bh);
                    break;
                }
            }
        }
    }

    // returns pointer to buffer data area or 0 if no space found
    BufferHeader*
    RingBuffer::get_new_buffer (ssize_t const size)
    {
        uint8_t* ret = next_;
        ssize_t const size_next (size + sizeof(BufferHeader));

        if (ret >= first_) {
            assert (0 == size_trail_);
            // try to find space at the end
            register ssize_t const end_size(end_ - ret);
            if (end_size >= size_next) {
                goto found_space;
            }
            else {
                // no space at the end, go from the start
                size_trail_ = end_size;
                ret = start_;
            }
        }

        while ((first_ - ret) < size_next) {
            // try to discard first buffer to get more space
            BufferHeader* bh = BH_cast(first_);

            // this will be automatically true also when (first_ == next_)
            if (!BH_is_released(bh))
            {
                // can't free any more space, so no buffer, and check trailing
                if (next_ > first_) size_trail_ = 0;
                return 0;
            }

            if (bh->seqno_g != SEQNO_NONE) discard_seqno (bh->seqno_g);

            first_ += bh->size;

            if (0 == (BH_cast(first_))->size /*&& first_ != next_*/)
            {
                // empty header and not next: check if we fit at the end
                // and roll over if not
                first_ = start_;
                size_trail_ = 0; // we're now contiguous: first_ <= next_
                if ((end_ - ret) >= size_next) {
                    goto found_space;
                }
                else {
                    ret = start_;
                }
            }
        }

#ifndef NDEBUG
        if ((first_ - ret) < size_next) {
            log_fatal << "Assertion ((first - ret) >= size_next) failed: "
                      << std::endl
                      << "first offt = " << (first_ - start_) << std::endl
                      << "next  offt = " << (next_  - start_) << std::endl
                      << "end   offt = " << (end_   - start_) << std::endl
                      << "ret   offt = " << (ret    - start_) << std::endl
                      << "size_next  = " << size_next         << std::endl;
            abort();
        }
#endif

    found_space:
        size_used_ += size;
        assert (size_used_ <= size_cache_);
        size_free_ -= size;
        assert (size_free_ >= 0);

        next_ = ret + size;
        assert (next_ + sizeof(BufferHeader) <= end_);

        BH_clear (BH_cast(next_));

        BufferHeader* bh = BH_cast(ret);
        bh->size    = size;
        bh->seqno_g = SEQNO_NONE;
        bh->seqno_d = SEQNO_ILL;
        bh->flags   = 0;
        bh->store   = BUFFER_IN_RB;
        bh->ctx     = this;

        return bh;
    }

    void* 
    RingBuffer::malloc (ssize_t size) throw ()
    {
        // We can reliably allocate continuous buffer which is 1/2
        // of a total cache space. So compare to half the space
        if (size <= (size_cache_ / 2) && size <= (size_cache_ - size_used_))
        {
            BufferHeader* const bh (get_new_buffer (size));

//            mallocs_++;

            if (gu_likely (0 != bh)) return (bh + 1);
        }

        return 0; // "out of memory"
    }

    void
    RingBuffer::free (const void* ptr) throw ()
    {
        if (gu_likely(NULL != ptr))
        {
            BufferHeader* bh = ptr2BH(ptr);

            size_used_ -= bh->size;
            assert(size_used_ >= 0);
            // space is unused but not free
            // space counted as free only when it is erased from the map
            BH_release (bh);
            if (SEQNO_NONE == bh->seqno_g) discard (bh);
        }
    }

    void*
    RingBuffer::realloc (void* ptr, ssize_t size) throw ()
    {
        // We can reliably allocate continuous buffer which is twice as small
        // as total cache area. So compare to half the space
        if (size > (size_cache_ / 2)) return 0;

        BufferHeader* bh = ptr2BH(ptr);

//        reallocs_++;

        // first check if we can grow this buffer by allocating
        // adjacent buffer
        {
            uint8_t* adj_ptr  = reinterpret_cast<uint8_t*>(bh) + bh->size;
            size_t   adj_size = size - bh->size;

            if (adj_ptr == next_)
            {
                void* const adj_buf (get_new_buffer (adj_size));

                if (adj_ptr == adj_buf)
                {
                    bh->size = size;
                    return ptr;
                }
                else // adjacent buffer allocation failed, return it back
                {
                    next_ = adj_ptr;
                    size_used_ -= adj_size;
                    size_free_ += adj_size;
                }
            }
        }

        // find non-adjacent buffer
        void* ptr_new = malloc (size);
        if (ptr_new != 0) {
            memcpy (ptr_new, ptr, bh->size - sizeof(BufferHeader));
            free (ptr);
        }

        return ptr_new;
    }

    void
    RingBuffer::seqno_reset()
    {
        if (size_cache_ == size_free_) return;

        /* find the last seqno'd RB buffer */
        BufferHeader* bh(0);

        for (seqno2ptr_t::reverse_iterator r(seqno2ptr_.rbegin());
             r != seqno2ptr_.rend(); ++r)
        {
            BufferHeader* const b(ptr2BH(r->second));
            if (BUFFER_IN_RB == b->store)
            {
                assert(BH_is_released(b));
                bh = b;
                break;
            }
        }

        if (!bh) return;

        /* This should be called in isolation, when all seqno'd buffers are
         * freed, and the only unreleased buffers should come only from new
         * configuration. Find the first unreleased buffer. There should be
         * no seqno'd buffers after it. */

        while (BH_is_released(bh)) // next_ is never released - no endless loop
        {
             first_ = reinterpret_cast<uint8_t*>(bh) + bh->size;
             bh = BH_cast(first_);

             if (gu_unlikely (0 == bh->size && first_ != next_))
             {
                 first_ = start_;
                 bh = BH_cast(first_);
             }
        }

        if (first_ == next_)
        {
            log_info << "GCache DEBUG: RingBuffer::seqno_reset(): full reset";
            /* empty RB, reset it completely */
            reset();
            return;
        }

        assert ((BH_cast(first_))->size > 0);
        assert ((BH_cast(first_))->seqno_g == SEQNO_NONE);
        assert (!BH_is_released(BH_cast(first_)));

        /* Find how much space remains */
        ssize_t const old(size_free_);
        if (first_ < next_)
        {
            /* start_  first_      next_    end_ 
             *   |       |###########|       |
             */
            size_used_ = next_ - first_;
            size_free_ = size_cache_ - size_used_;
            size_trail_= 0;
        }
        else
        {
            /* start_  next_       first_   end_ 
             *   |#######|           |#####| |
             *                              ^size_trail_ */
            size_free_ = first_ - next_ + size_trail_;
            size_used_ = size_cache_ - size_free_;
        }

        log_info << "GCache DEBUG: RingBuffer::seqno_reset(): discarded "
                 << (size_free_ - old) << " bytes";

        /* There is a small but non-0 probability that some seqno'd buffers
         * are locked withing yet unreleased aborted local actions.
         * Seek all the way to next_ and invalidate seqnos */

        long total(0);
        long locked(0);

        bh = BH_cast(first_ + (BH_cast(first_))->size);

        while (bh != BH_cast(next_))
        {
            if (gu_likely (bh->size > 0))
            {
                total++;
                if (bh->seqno_g != SEQNO_NONE)
                {
                    assert (BH_is_released(bh));
                    bh->seqno_g = SEQNO_NONE;
                    locked++;
                }
                bh = BH_cast(reinterpret_cast<uint8_t*>(bh) + bh->size);
            }
            else
            {
                assert (BH_cast(next_) < bh);
                bh = BH_cast(start_);
            }
        }

        log_info << "GCache DEBUG: RingBuffer::seqno_reset(): found "
                 << locked << '/' << total << " locked buffers";
    }

    std::ostream& operator<< (std::ostream& os, const RingBuffer& rb)
    {
        os  << "\nstart_ : " << reinterpret_cast<void*>(rb.start_)
            << "\nend_   : " << reinterpret_cast<void*>(rb.end_)
            << "\nfirst  : " << rb.first_ - rb.start_
            << "\nnext   : " << rb.next_ - rb.start_
            << "\nsize   : " << rb.size_cache_
            << "\nfree   : " << rb.size_free_
            << "\nused   : " << rb.size_used_;
        return os;
    }
}
