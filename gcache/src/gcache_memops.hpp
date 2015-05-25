/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

/*! @file memory operations interface */

#ifndef _gcache_memops_hpp_
#define _gcache_memops_hpp_

namespace gcache
{
    struct BufferHeader;

    class MemOps
    {
    public:
        MemOps() {}
        virtual ~MemOps() {}

        virtual void*
        malloc  (int size)                = 0;

        virtual void
        free    (BufferHeader* bh)        = 0;

        virtual void*
        realloc (void* ptr, int size)     = 0;

        virtual void
        discard (BufferHeader* bh)        = 0;

        virtual void
        reset   ()                        = 0;
    };
}

#endif /* _gcache_memops_hpp_ */
