/*
 * Copyright (C) 2009-2016 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_mmap.hpp"

#include "gu_logger.hpp"
#include "gu_throw.hpp"
#include "gu_macros.hpp"

#include "gu_limits.h" // GU_PAGE_SIZE

#include <cerrno>
#include <sys/mman.h>

#if defined(__FreeBSD__) && defined(MAP_NORESERVE)
/* FreeBSD has never implemented this flags and will deprecate it. */
#undef MAP_NORESERVE
#endif

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

// to avoid -Wold-style-cast
extern "C" { static const void* const GU_MAP_FAILED = MAP_FAILED; }

namespace gu
{
    MMap::MMap (const FileDescriptor& fd, bool const sequential)
        :
        size   (fd.size()),
        ptr    (mmap (NULL, size, PROT_READ|PROT_WRITE,
                      MAP_SHARED|MAP_NORESERVE, fd.get(), 0)),
        mapped (ptr != GU_MAP_FAILED)
    {
        if (!mapped)
        {
            gu_throw_error(errno) << "mmap() on '" << fd.name()
                                  << "' failed";
        }

#if defined(MADV_DONTFORK)
        if (posix_madvise (ptr, size, MADV_DONTFORK))
        {
#   define MMAP_INHERIT_OPTION "MADV_DONTFORK"
#elif defined(__FreeBSD__)
        if (minherit (ptr, size, INHERIT_NONE))
        {
#   define MMAP_INHERIT_OPTION "INHERIT_NONE"
#endif
#if defined(MMAP_INHERIT_OPTION)
            int const err(errno);
            log_warn << "Failed to set " MMAP_INHERIT_OPTION " on " << fd.name()
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
        if (posix_madvise(reinterpret_cast<char*>(ptr), size, MADV_DONTNEED))
        {
            log_warn << "Failed to set MADV_DONTNEED on " << ptr << ": "
                     << errno << " (" << strerror(errno) << ')';
        }
    }

    void
    MMap::sync(void* const addr, size_t const length) const
    {
        /* libc msync() only accepts addresses multiple of page size,
         * rounding down */
        static uint64_t const PAGE_SIZE_MASK(~(GU_PAGE_SIZE - 1));

        uint8_t* const sync_addr(reinterpret_cast<uint8_t*>
                                 (uint64_t(addr) & PAGE_SIZE_MASK));
        size_t   const sync_length
            (length + (static_cast<uint8_t*>(addr) - sync_addr));

        if (::msync(sync_addr, sync_length, MS_SYNC) < 0)
        {
            gu_throw_error(errno) << "msync(" << sync_addr << ", "
                                  << sync_length << ") failed";
        }
    }

    void
    MMap::sync () const
    {
        log_info << "Flushing memory map to disk...";
        sync(ptr, size);
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
        if (mapped)
        {
           try { unmap(); } catch (Exception& e) { log_error << e.what(); }
        }
    }
}
