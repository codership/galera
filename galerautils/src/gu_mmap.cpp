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
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include "gu_limits.h"

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

#if !defined(__sun__) && !defined(__APPLE__) && !defined(__FreeBSD__)
        /* Solaris, Darwin, and FreeBSD do not have MADV_DONTFORK */
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
        if (posix_madvise(reinterpret_cast<char*>(ptr), size, MADV_DONTNEED))
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

/** Returns actual memory usage by allocated page range: **/

size_t gu_actual_memory_usage (const void * const ptr, const size_t length)
{
    size_t size= 0;
    if (length)
    {       
      const uintptr_t first= (uintptr_t) ptr                & -GU_PAGE_SIZE;
      const uintptr_t last=  ((uintptr_t) ptr + length - 1) & -GU_PAGE_SIZE;
      const ptrdiff_t total= last - first + GU_PAGE_SIZE;
      const size_t    pages= total / GU_PAGE_SIZE;
      unsigned char * const map= (unsigned char *) malloc(pages);
      if (map)
      {
        if (mincore((void *) first, total, map) == 0)
        {
          for (size_t i = 0; i < pages; i++)
          {
            if (map[i])
              size += GU_PAGE_SIZE;
          }
        }
        else
        {
           log_fatal << "Unable to get in-core state vector for page range. "
                     << "Aborting.";
           abort();
        }
        free(map);
      }
      else
      {
        log_fatal << "Unable to allocate memory for in-core state vector. "
                  << "Aborting.";
        abort();
      }
    }
    return size;
}
