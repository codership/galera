/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_MMAP__
#define __GCACHE_MMAP__

#include "FileDescriptor.hpp"

namespace gcache
{
    class MMap
    {

    private:

        void*  const value;
        size_t const length;

    public:

        MMap (size_t length, const FileDescriptor& fd);

        virtual ~MMap ();

        void* get() const throw() { return value; };

    private:

        // This class is definitely non-copyable
        MMap (const MMap&);
        MMap& operator = (const MMap);
    };
}

#endif /* __GCACHE_MMAP__ */
