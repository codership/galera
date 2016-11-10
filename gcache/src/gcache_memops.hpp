/*
 * Copyright (C) 2010-2015 Codership Oy <info@codership.com>
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

        /* although size value passed to GCache should be representable by
         * a signed integer type, internally the buffer allocated will also
         * incur header overhead, so it has to be represented by unsigned int.
         * However the difference between two internal sizes should never exceed
         * signed representation. */
        typedef          int ssize_type; // size passed to GCache
        typedef unsigned int size_type;  // internal size representation
        typedef ssize_type   diff_type;  // difference between two size_types

        MemOps() {}
        virtual ~MemOps() {}

        virtual void*
        malloc  (size_type size)          = 0;

        virtual void
        free    (BufferHeader* bh)        = 0;

        virtual void*
        realloc (void* ptr, size_type size) = 0;

        virtual void
        discard (BufferHeader* bh)        = 0;

        virtual void
        reset   ()                        = 0;
    };
}

#endif /* _gcache_memops_hpp_ */
