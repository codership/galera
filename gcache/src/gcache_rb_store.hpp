/*
 * Copyright (C) 2010-2016 Codership Oy <info@codership.com>
 */

/*! @file ring buffer storage class */

#ifndef _gcache_rb_store_hpp_
#define _gcache_rb_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_bh.hpp"
#include "gcache_types.hpp"

#include <gu_fdesc.hpp>
#include <gu_mmap.hpp>
#include <gu_uuid.hpp>

#include <string>

namespace gcache
{
    class RingBuffer : public MemOps
    {
    public:

        RingBuffer (const std::string& name,
                    size_t             size,
                    seqno2ptr_t&       seqno2ptr,
                    gu::UUID&          gid,
                    bool               recover);

        ~RingBuffer ();

        void* malloc  (size_type size);

        void  free    (BufferHeader* bh);

        void* realloc (void* ptr, size_type size);

        void  discard (BufferHeader* const bh)
        {
            assert (BH_is_released(bh));
            assert (SEQNO_ILL == bh->seqno_g);
            size_free_ += bh->size;
            assert (size_free_ <= size_cache_);
        }

        size_t size      () const { return size_cache_; }

        size_t rb_size   () const { return fd_.size(); }

        const std::string& rb_name() const { return fd_.name(); }

        void  reset();

        void  seqno_reset();

        /* returns true when successfully discards all seqnos in range */
        bool  discard_seqnos(seqno2ptr_t::iterator i_begin,
                             seqno2ptr_t::iterator i_end);

        /* returns true when successfully discards all seqnos up to s */
        bool  discard_seqno(seqno_t s)
        {
            return discard_seqnos(seqno2ptr_.begin(), seqno2ptr_.find(s + 1));
        }

        void print (std::ostream& os) const;

        static size_t pad_size()
        {
            RingBuffer* rb(0);
            // cppcheck-suppress nullPointer
            return (PREAMBLE_LEN * sizeof(*(rb->preamble_)) +
                    // cppcheck-suppress nullPointer
                    HEADER_LEN   * sizeof(*(rb->header_)));
        }

        void assert_size_free() const
        {
#ifndef NDEBUG
            if (next_ >= first_)
            {
                /* start_  first_      next_    end_
                 *   |       |###########|       |      */
                assert(size_free_ >= (size_cache_ - (next_ - first_)));
            }
            else
            {
                /* start_  next_       first_   end_
                 *   |#######|           |#####| |      */
                assert(size_free_ >= size_t(first_ - next_));
            }
            assert (size_free_ <= size_cache_);
#endif
        }

        void assert_size_trail() const
        {
#ifndef NDEBUG
            if (next_ >= first_)
                assert(0 == size_trail_);
            else
                assert(size_trail_ >= sizeof(BufferHeader));
#endif
        }

        void assert_sizes() const
        {
            assert_size_trail();
            assert_size_free();
        }

        size_t allocated_pool_size ();

    private:

        static size_t const PREAMBLE_LEN = 1024;
        static size_t const HEADER_LEN = 32;

        gu::FileDescriptor fd_;
        gu::MMap           mmap_;
        char*        const preamble_; // ASCII text preamble
        int64_t*     const header_;   // cache binary header
        uint8_t*     const start_;    // start of cache area
        uint8_t*     const end_;      // first byte after cache area
        uint8_t*           first_;    // pointer to the first (oldest) buffer
        uint8_t*           next_;     // pointer to the next free space

        size_t            max_used_; // maximal memory usage (in bytes)
        seqno2ptr_t&       seqno2ptr_;
        gu::UUID&          gid_;

        size_t       const size_cache_;
        size_t             size_free_;
        size_t             size_used_;
        size_t             size_trail_;

        bool               open_;

        BufferHeader* get_new_buffer (size_type size);

        void          constructor_common();

        /* preamble fields */
        static std::string const PR_KEY_VERSION;
        static std::string const PR_KEY_GID;
        static std::string const PR_KEY_SEQNO_MAX;
        static std::string const PR_KEY_SEQNO_MIN;
        static std::string const PR_KEY_OFFSET;
        static std::string const PR_KEY_SYNCED;

        void          write_preamble(bool synced);
        void          open_preamble(bool recover);
        void          close_preamble();

        // returns lower bound (not inclusive) of valid seqno range
        int64_t       scan(off_t offset);
        void          recover(off_t offset);

        void          estimate_space();

        RingBuffer(const gcache::RingBuffer&);
        RingBuffer& operator=(const gcache::RingBuffer&);

#ifdef GCACHE_RB_UNIT_TEST
    public:
        uint8_t* start() const { return start_; }
#endif
    };

    inline std::ostream& operator<< (std::ostream& os, const RingBuffer& rb)
    {
        rb.print(os);
        return os;
    }

} /* namespace gcache */

#endif /* _gcache_rb_store_hpp_ */
