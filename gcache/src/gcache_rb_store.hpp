/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

/*! @file ring buffer storage class */

#ifndef _gcache_rb_store_hpp_
#define _gcache_rb_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_fd.hpp"
#include "gcache_mmap.hpp"
#include "gcache_bh.hpp"

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

        void  free    (void*  ptr)  throw();

        void* realloc (void*  ptr, ssize_t size) throw ();

        void  discard (BufferHeader* bh) throw ()
        {
            size_free_ += bh->size;
        }

        ssize_t size () const throw() /* total page size */
        { 
            return fd_.get_size() - sizeof(BufferHeader);
        }

        const std::string& name()
        {
            return fd_.get_name();
        }

        void  reset();

        void  discard_seqno  (int64_t seqno);

    private:

        FileDescriptor  fd_;
        MMap            mmap_;
        bool            open_;
        char*     const preamble_; // ASCII text preamble
        int64_t*  const header_;   // cache binary header
        ssize_t   const header_len_;
        uint8_t*  const start_;    // start of cache area
        uint8_t*  const end_;      // first byte after cache area
        uint8_t*        first_;    // pointer to the first (oldest) buffer
        uint8_t*        next_;     // pointer to the next free space

        ssize_t   const size_cache_;
        ssize_t         size_free_;
        ssize_t         size_used_;

        long long       mallocs_;
        long long       reallocs_;

        typedef std::map<int64_t, const void*> seqno2ptr_t;

        seqno2ptr_t&    seqno2ptr_;

        void* get_new_buffer (ssize_t size);

        void  constructor_common();

        RingBuffer(const gcache::RingBuffer&);
        RingBuffer& operator=(const gcache::RingBuffer&);
    };
}

#endif /* _gcache_rb_store_hpp_ */
