/*
 * Copyright (C) 2010-2011 Codership Oy <info@codership.com>
 */

/*! @file page file class */

#ifndef _gcache_mem_store_hpp_
#define _gcache_mem_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_fd.hpp"
#include "gcache_mmap.hpp"
#include "gcache_bh.hpp"

#include <string>

namespace gcache
{
    class MemStore : public MemOps
    {
    public:

        MemStore (ssize_t max_size) throw ()
            : max_size_(max_size), size_(0), count_(0)
        {}

        ~MemStore () {}

        void* malloc  (ssize_t size) throw ()
        {
            if (size + size_ <= max_size_)
            {
                BufferHeader* bh(reinterpret_cast<BufferHeader*>(malloc(size)));

                if (gu_likely(0 != bh))
                {
                    bh->size  = size;
                    bh->seqno = SEQNO_NONE;
                    bh->flags = 0;
                    bh->store = BUFFER_IN_MEM;
                    bh->ctx   = this;

                    size_ += size;
                    count_++;

                    return (bh + 1);
                }
            }

            return 0;
        }

        void  free    (void*  ptr)  throw()
        {
            if (gu_likely (0 != ptr))
            {
                BufferHeader* const bh(ptr2BH(ptr));

                assert(bh->size > 0);
                assert(bh->size <= size_);
                assert(bh->store == BUFFER_IN_MEM);
                assert(bh->ctx == this);

                size_ -= bh->size;
                count_--;

                BH_release (bh);
                if (SEQNO_NONE == bh->seqno) free (bh);
            }
        }

        void* realloc (void*  ptr, ssize_t size) throw ()
        {
            BufferHeader* bh(0);
            ssize_t old_size(0);

            if (ptr)
            {
                bh = ptr2BH(ptr);
                old_size = bh->size;
            }

            if (size_ + size - old_size <= max_size_)
            {
                void* tmp = realloc (bh, size);

                if (tmp)
                {
                    bh = ptr2BH(tmp);
                    assert (bh->size == old_size);
                    bh->size = size;

                    size_ += size - old_size;

                    return (bh + 1);
                }
            }

            return 0;
        }

        ssize_t count () const throw() { return count_; }

        void set_max_size (ssize_t size) throw() { max_size_ = size; }

    private:

        ssize_t        max_size_;
        ssize_t        size_;
        ssize_t        count_;
    };
}

#endif /* _gcache_mem_store_hpp_ */

