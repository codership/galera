/*
 * Copyright (C) 2010-2013 Codership Oy <info@codership.com>
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
                    std::map<int64_t, const void*>& seqno2ptr)
            throw (gu::Exception);

        ~RingBuffer ();

        void* malloc  (ssize_t size) throw ();

        void  free    (const void* ptr)  throw();

        void* realloc (void* ptr, ssize_t size) throw ();

        void  discard (BufferHeader* bh) throw ()
        {
            size_free_ += bh->size;
        }

        ssize_t size      () const throw() { return size_cache_ ; }

        ssize_t rb_size   () const throw() { return fd_.size(); }

        const std::string& rb_name() const throw() { return fd_.name(); }

        void  reset();

        void  seqno_reset();

        void  discard_seqno  (int64_t seqno);

        static ssize_t pad_size()
        {
            RingBuffer* rb(0);
            return (PREAMBLE_LEN * sizeof(*(rb->preamble_)) +
                    HEADER_LEN * sizeof(*(rb->header_)));
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

//        long long       mallocs_;
//        long long       reallocs_;

        typedef std::map<int64_t, const void*> seqno2ptr_t;

        seqno2ptr_t&    seqno2ptr_;

        BufferHeader* get_new_buffer (ssize_t size);

        void  constructor_common();

        RingBuffer(const gcache::RingBuffer&);
        RingBuffer& operator=(const gcache::RingBuffer&);

        friend std::ostream& operator<< (std::ostream&, const RingBuffer&);
    };

    std::ostream& operator<< (std::ostream&, const RingBuffer&);
}

#endif /* _gcache_rb_store_hpp_ */
