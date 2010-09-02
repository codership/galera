/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_MMAP__
#define __GCACHE_MMAP__

#include "gcache_fd.hpp"

namespace gcache
{
    class MMap
    {

    public:

        size_t const size;
        void*  const ptr;

        MMap (const FileDescriptor& fd);

        virtual ~MMap ();

        void  sync() const;
        void  unmap();

    private:

        bool  mapped;

        // This class is definitely non-copyable
        MMap (const MMap&);
        MMap& operator = (const MMap);
    };
}

#endif /* __GCACHE_MMAP__ */
