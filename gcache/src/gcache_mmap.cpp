/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include "gcache_mmap.hpp"

#include <galerautils.hpp>

#include <cerrno>
#include <sys/mman.h>

// to avoid -Wold-style-cast
extern "C" { static const void* const GCACHE_MAP_FAILED = MAP_FAILED; }

namespace gcache
{
    MMap::MMap (const FileDescriptor& fd)
        :
        size   (fd.get_size()),
        ptr    (mmap (NULL, size, PROT_READ|PROT_WRITE,
                      MAP_SHARED|MAP_NORESERVE, fd.get(), 0)),
        mapped (ptr != GCACHE_MAP_FAILED)
    {
        if (!mapped)
        {
            int err = errno;
            gu_throw_error(err) << "mmap() on '" << fd.get_name() << "' failed";
        }

        log_debug << "Memory mapped: " << ptr << " (" << size << " bytes)";
    }

    void
    MMap::sync () const
    {
        log_info << "Flushing memory map to disk...";

        if (msync (ptr, size, MS_SYNC) < 0)
        {
            int err = errno;
            gu_throw_error(err) << "msync(" << ptr << ", " << size <<") failed";
        }
    }

    void
    MMap::unmap ()
    {
        if (munmap (ptr, size) < 0)
        {
            int err = errno;
            gu_throw_error(err) << "munmap(" << ptr << ", " << size<<") failed";
        }

        mapped = false;

        log_debug << "Memory unmapped: " << ptr << " (" << size <<" bytes)";
    }

    MMap::~MMap ()
    {
        if (mapped) unmap();
    }
}
