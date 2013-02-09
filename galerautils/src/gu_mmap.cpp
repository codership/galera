/*
 * Copyright (C) 2009-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_mmap.hpp"

#include "gu_logger.hpp"
#include "gu_throw.hpp"

#include <cerrno>
#include <sys/mman.h>

// to avoid -Wold-style-cast
extern "C" { static const void* const GCACHE_MAP_FAILED = MAP_FAILED; }

namespace gu
{
    MMap::MMap (const FileDescriptor& fd, bool const sequential)
        :
        size   (fd.size()),
        ptr    (mmap (NULL, size, PROT_READ|PROT_WRITE,
                      MAP_SHARED|MAP_NORESERVE, fd.get(), 0)),
        mapped (ptr != GCACHE_MAP_FAILED)
    {
        if (!mapped)
        {
            gu_throw_error(errno) << "mmap() on '" << fd.name()
                                  << "' failed";
        }

#if !defined(__sun__) /* Solaris does not have MADV_DONTFORK */
        if (posix_madvise (ptr, size, MADV_DONTFORK))
        {
            int const err(errno);
            log_warn << "Failed to set MADV_DONTFORK on " << fd.name()
                     << ": " << err << " (" << strerror(err) << ")";
        }
#endif

        /* benefits are questionable */
        if (sequential && posix_madvise (ptr, size, MADV_SEQUENTIAL))
        {
            int const err(errno);
            log_warn << "Failed to set MADV_SEQUENTIAL on " << fd.name()
                     << ": " << err << " (" << strerror(err) << ")";
        }

        log_debug << "Memory mapped: " << ptr << " (" << size << " bytes)";
    }

    void
    MMap::dont_need() const
    {
        if (madvise(reinterpret_cast<char*>(ptr), size, MADV_DONTNEED))
        {
            log_warn << "Failed to set MADV_DONTNEED on " << ptr << ": "
                     << errno << " (" << strerror(errno) << ')';
        }
    }

    void
    MMap::sync () const
    {
        log_info << "Flushing memory map to disk...";

        if (msync (ptr, size, MS_SYNC) < 0)
        {
            gu_throw_error(errno) << "msync(" << ptr << ", " << size
                                  << ") failed";
        }
    }

    void
    MMap::unmap ()
    {
        if (munmap (ptr, size) < 0)
        {
            gu_throw_error(errno) << "munmap(" << ptr << ", " << size
                                  << ") failed";
        }

        mapped = false;

        log_debug << "Memory unmapped: " << ptr << " (" << size <<" bytes)";
    }

    MMap::~MMap ()
    {
        if (mapped) unmap();
    }
}
