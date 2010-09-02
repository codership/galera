/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

/*! @file page file class */

#ifndef _gcache_page_hpp_
#define _gcache_page_hpp_

#include "gcache_memops.hpp"
#include "gcache_fd.hpp"
#include "gcache_mmap.hpp"
#include "gcache_bh.hpp"

#include <string>

namespace gcache
{
    class Page : public MemOps
    {
    public:

        Page (const std::string& name, ssize_t size) throw (gu::Exception);
        ~Page () {}

        void* malloc  (ssize_t size) throw ();

        void  free    (void*  ptr)  throw()
        {
            assert (ptr > mmap_.ptr);
            assert (ptr <= (static_cast<uint8_t*>(mmap_.ptr) + fd_.get_size()));
            assert (count_ > 0);
            count_--;
            BH_release (ptr2BH(ptr));
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

        FileDescriptor fd_;
        MMap           mmap_;
        uint8_t*       next_;
        ssize_t        size_;
        ssize_t        count_;

        Page(const gcache::Page&);
        Page& operator=(const gcache::Page&);
    };
}

#endif /* _gcache_page_hpp_ */
