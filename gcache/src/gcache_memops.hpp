/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

/*! @file memory operations interface */

#ifndef _gcache_memops_hpp_
#define _gcache_memops_hpp_

#include <galerautils.hpp>

namespace gcache
{
    struct BufferHeader;

    class MemOps
    {
    public:
        MemOps() {}
        virtual ~MemOps() {}

        virtual void*
        malloc  (ssize_t size)            = 0;

        virtual void
        free    (BufferHeader* bh)        = 0;

        virtual void*
        realloc (void* ptr, ssize_t size) = 0;

        virtual void
        discard (BufferHeader* bh)        = 0;

        virtual void
        reset   ()                        = 0;
    };
}

#endif /* _gcache_memops_hpp_ */
