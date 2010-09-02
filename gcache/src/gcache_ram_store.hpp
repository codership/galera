/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

/*! @file page file class */

#ifndef _gcache_ram_store_hpp_
#define _gcache_ram_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_fd.hpp"
#include "gcache_mmap.hpp"
#include "gcache_bh.hpp"

#include <string>

#error "Incomplete header"

namespace gcache
{
    class RamStore : public MemOps
    {
    public:

        RamStore (ssize_t max_size) throw () : max_size_(max_size), size_(0) {}
        ~RamStore () {}

        void* malloc  (ssize_t size) throw ()
        {
            size += sizeof(BufferHeader);

            if (size + size_ <= max_size_)
            {
                BufferHeader* ret = static_cast<BufferHeader*>::malloc(size);

                if (gu_likely(0 != ret)) size_ += size;

                return ret + 1;
            }
            else return 0;
        }

        void  free    (void*  ptr)  throw()
        {
            assert (ptr > mmap_.ptr());
            assert (ptr < (mmap_.ptr() + size_));
            assert (count_ > 0);
            count_--;
        }

        void* realloc (void*  ptr, ssize_t size) throw ();

        ssize_t count () const throw() { return count_; }

        ssize_t size () const throw() /* total page size */
        { 
            return fd_.get_size() - sizeof(BufferHeader);
        }

        const std::string& name()
        {
            return fd_.get_name();
        }

    private:

        ssize_t        max_size_;
        ssize_t        size_;

        RamStore(const RamStore&);
        RamStore& operator=(const RamStore&);
    };
}

#endif /* _gcache_ram_store_hpp_ */

