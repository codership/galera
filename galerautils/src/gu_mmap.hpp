/*
 * Copyright (C) 2009-2016 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef __GCACHE_MMAP__
#define __GCACHE_MMAP__

#include "gu_fdesc.hpp"

namespace gu
{

class MMap
{

public:

    size_t const size;
    void*  const ptr;

    MMap (const FileDescriptor& fd, bool sequential = false);

    ~MMap ();

    void dont_need() const;
    void sync(void *addr, size_t length) const;
    void sync() const;
    void unmap();

private:

    bool mapped;

    // This class is definitely non-copyable
    MMap (const MMap&);
    MMap& operator = (const MMap);
};

} /* namespace gu */

#endif /* __GCACHE_MMAP__ */
