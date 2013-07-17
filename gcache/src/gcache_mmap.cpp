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
            gu_throw_error(errno) << "mmap() on '" << fd.get_name()
                                  << "' failed";
        }
#if !defined(__sun__) && !defined(__APPLE__) && !defined(__FreeBSD__) /* Solaris, Darwin, and FreeBSD do not have MADV_DONTFORK */
        if (posix_madvise (ptr, size, MADV_DONTFORK))
        {
            int const err(errno);
            log_warn << "Failed to set MADV_DONTFORK on " << fd.get_name()
                     << ": " << err << " (" << strerror(err) << ")";
        }
#endif
/* benefits are questionable
        if (posix_madvise (ptr, size, MADV_SEQUENTIAL))
        {
            int const err(errno);
            log_warn << "Failed to set MADV_SEQUENTIAL on " << fd.get_name()
                     << ": " << err << " (" << strerror(err) << ")";
        }
*/
        log_debug << "Memory mapped: " << ptr << " (" << size << " bytes)";
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

    void
    MMap::dont_need() const
    {
        if (posix_madvise(reinterpret_cast<char*>(ptr), size, MADV_DONTNEED))
        {
            log_warn << "Failed to set MADV_DONTNEED on " << ptr << ": "
                     << errno << " (" << strerror(errno) << ')';
        }
    }

    MMap::~MMap ()
    {
        if (mapped) unmap();
    }
}
