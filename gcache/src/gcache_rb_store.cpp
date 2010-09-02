/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "gcache_rb_store.hpp"

#include <galerautils.hpp>
#include <cassert>

namespace gcache
{
    static size_t  const PREAMBLE_LEN = 1024; // reserved for text preamble

    static size_t check_size (ssize_t s)
    {
        if (s < 0) gu_throw_error(EINVAL) << "Negative cache file size: " << s;

        return s + PREAMBLE_LEN;
    }

    void
    RingBuffer::reset()
    {
        first_ = start_;
        next_  = start_;

        BH_clear (reinterpret_cast<BufferHeader*>(next_));

        size_free_ = size_cache_;
        size_used_ = 0;

        mallocs_  = 0;
        reallocs_ = 0;
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
        header_len_(32),
        start_     (reinterpret_cast<uint8_t*>(header_ + header_len_)),
        end_       (reinterpret_cast<uint8_t*>(preamble_ + mmap_.size)),
        first_     (start_),
        next_      (first_),
        size_cache_(end_ - start_),
        size_free_ (size_cache_),
        size_used_ (0),
        mallocs_   (0),
        reallocs_  (0),
        seqno2ptr_(seqno2ptr)
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
            bh->seqno = SEQNO_NONE;

            switch (bh->store)
            {
            case BUFFER_IN_RAM:
                /* add what to do when buffer is in RAM */
                break;
            case BUFFER_IN_RB:
                if (gu_likely(BH_is_released(bh))) discard_buffer (bh);
                break;
            }
        }
    }

    // returns pointer to buffer data area or 0 if no space found
    void*
    RingBuffer::get_new_buffer (ssize_t const size)
    {
        // reserve space for the closing header (this->next_)
        ssize_t const size_next = size + sizeof(BufferHeader);

        // don't even try if there's not enough unused space
        if (size_next > (size_cache_ - size_used_)) return 0;

        uint8_t* ret = next_;

        if (ret >= first_) {
            // try to find space at the end
            if ((end_ - ret) >= size_next) {
                goto found_space;
            }
            else {
                // no space at the end, go from the start
                ret = start_;
            }
        }

        while ((first_ - ret) < size_next) {
            // try to discard first buffer to get more space
            BufferHeader* bh = reinterpret_cast<BufferHeader*>(first_);

            // this will be automatically true also when (first_ == next_)
            if (!BH_is_released(bh))
                return 0; // can't free any more space, so no buffer

            if (bh->seqno != SEQNO_NONE) discard_seqno (bh->seqno);

            first_ += bh->size;

            if (0      == (reinterpret_cast<BufferHeader*>(first_))->size &&
                first_ != next_)
            {
                // empty header and not next: check if we fit at the end
                // and roll over if not
                first_ = start_;
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
        assert (next_ + sizeof(BufferHeader) < end_);

        BH_clear (reinterpret_cast<BufferHeader*>(next_));

        BufferHeader* bh = reinterpret_cast<BufferHeader*>(ret);
        bh->size  = size;
        bh->seqno = SEQNO_NONE;
        bh->flags = 0;
        bh->store = BUFFER_IN_RB;
        bh->ctx   = this;

        return (bh + 1); // pointer to data area
    }

    void* 
    RingBuffer::malloc (ssize_t size) throw ()
    {
        size = size + sizeof(BufferHeader);

        // We can reliably allocate continuous buffer which is twice as small
        // as total cache area. So compare to half the space
        if (static_cast<ssize_t>(size) < (size_cache_ / 2))
        {
            void*    ptr;

            mallocs_++;

            if (0 == (ptr = get_new_buffer (size))) return 0;

            assert (0 != ptr);

            return (ptr);
        }

        return 0; // "out of memory"
    }

    void
    RingBuffer::free (void* ptr) throw ()
    {
        if (gu_likely(NULL != ptr))
        {
            BufferHeader* bh = ptr2BH(ptr);

            size_used_ -= bh->size;
            assert(size_used_ >= 0);
            // space is unused but not free
            // space counted as free only when it is erased from the map
            BH_release (bh);
            if (SEQNO_NONE == bh->seqno) discard_buffer(bh);
        }
    }

    void*
    RingBuffer::realloc (void* ptr, ssize_t size) throw ()
    {
        size = size + sizeof(BufferHeader);

        // We can reliably allocate continuous buffer which is twice as small
        // as total cache area. So compare to half the space
        if (size >= (size_cache_ / 2)) return 0;

        BufferHeader* bh = ptr2BH(ptr);

        // first check if we can grow this buffer by allocating
        // adjacent buffer
        {
            uint8_t* adj_ptr  = reinterpret_cast<uint8_t*>(bh) + bh->size;
            size_t   adj_size = size - bh->size;
            void*    adj_buff = 0;

            reallocs_++;
            // do the same stuff as in malloc(), conditional that we have
            // adjacent space
            while ((adj_ptr == next_) &&
                   (0 == (adj_buff = get_new_buffer (adj_size)))) {
                return 0;
            }

            if (0 != adj_buff) {
#ifndef NDEBUG
                bool fail = false;
                if (adj_ptr != adj_buff) {
                    log_fatal << "Assertion (adj_ptr == adj_buff) failed: "
                              << adj_ptr << " != " << adj_buff;
                    fail = true;
                }
                if (adj_ptr + adj_size != next_) {
                    log_fatal << "Assertion (adj_ptr + adj_size == next) "
                              << "failed: " << (adj_ptr + adj_size) << " != "
                              << next_;
                    fail = true;
                }
                if (fail) abort();
#endif
                // allocated adjacent space, buffer pointer not changed
                return ptr;
            }
        }

        // find non-adjacent buffer
        void* ptr_new = this->malloc (size);
        if (ptr_new != 0) {
            memcpy (ptr_new, ptr, bh->size - sizeof(BufferHeader));
            this->free (ptr);
        }

        return ptr_new;
    } 
}
