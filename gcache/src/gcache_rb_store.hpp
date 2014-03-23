/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

/*! @file ring buffer storage class */

#ifndef _gcache_rb_store_hpp_
#define _gcache_rb_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_bh.hpp"

#include "gu_fdesc.hpp"
#include "gu_mmap.hpp"

#include <string>
#include <map>
#include <stdint.h>

namespace gcache
{
    class RingBuffer : public MemOps
    {
    public:

        RingBuffer (const std::string& name, ssize_t size,
                    std::map<int64_t, const void*>& seqno2ptr);

        ~RingBuffer ();

        void* malloc  (ssize_t size);

        void  free    (BufferHeader* bh);

        void* realloc (void* ptr, ssize_t size);

        void  discard (BufferHeader* const bh)
        {
            assert (BH_is_released(bh));
            assert (SEQNO_ILL == bh->seqno_g);
            size_free_ += bh->size;
            assert (size_free_ <= size_cache_);
        }

        ssize_t size      () const { return size_cache_; }

        ssize_t rb_size   () const { return fd_.size(); }

        const std::string& rb_name() const { return fd_.name(); }

        void  reset();

        void  seqno_reset();

        /* returns true when successfully discards all seqnos up to s */
        bool  discard_seqno  (int64_t s);

        void print (std::ostream& os) const;

        static ssize_t pad_size()
        {
            RingBuffer* rb(0);
            // cppcheck-suppress nullPounter
            return (PREAMBLE_LEN * sizeof(*(rb->preamble_)) +
                    HEADER_LEN   * sizeof(*(rb->header_)));
        }

        void assert_size_free() const
        {
#ifndef NDEBUG
            if (next_ > first_)
            {
                /* start_  first_      next_    end_
                 *   |       |###########|       |      */
                assert(size_free_ >= (size_cache_ - (next_ - first_)));
            }
            else
            {
                /* start_  next_       first_   end_
                 *   |#######|           |#####| |      */
                assert(size_free_ >= (first_ - next_));
            }
            assert (size_free_ <= size_cache_);
#endif
        }

    private:

        static ssize_t const PREAMBLE_LEN = 1024;
        static ssize_t const HEADER_LEN = 32;

        gu::FileDescriptor fd_;
        gu::MMap           mmap_;
        bool               open_;
        char*        const preamble_; // ASCII text preamble
        int64_t*     const header_;   // cache binary header
        uint8_t*     const start_;    // start of cache area
        uint8_t*     const end_;      // first byte after cache area
        uint8_t*           first_;    // pointer to the first (oldest) buffer
        uint8_t*           next_;     // pointer to the next free space

        ssize_t      const size_cache_;
        ssize_t            size_free_;
        ssize_t            size_used_;
        ssize_t            size_trail_;

        typedef std::map<int64_t, const void*> seqno2ptr_t;

        seqno2ptr_t&    seqno2ptr_;

        BufferHeader*   get_new_buffer (ssize_t size);

        void            constructor_common();

        RingBuffer(const gcache::RingBuffer&);
        RingBuffer& operator=(const gcache::RingBuffer&);
    };

    inline std::ostream& operator<< (std::ostream& os, const RingBuffer& rb)
    {
        rb.print(os);
        return os;
    }

} /* namespace gcache */

#endif /* _gcache_rb_store_hpp_ */
