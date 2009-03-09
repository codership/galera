/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include <cerrno>

// file descriptor related stuff
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

#include "Exception.hpp"
#include "Logger.hpp"
#include "Lock.hpp"
#include "GCache.hpp"

namespace gcache
{
    const size_t  PREAMBLE_SIZE = 1024; // reserved for text preamble
    const size_t  HEADER_SIZE   = 1024; // reserved for binary header
    const int64_t SEQNO_NONE    = 0;

    static size_t check_size (size_t megs)
    {
        // overflow check
        if (megs != ((megs << 20) >> 20)) {
            std::ostringstream msg;
            msg << "Requested cache size too high: " << megs << "Mb";
            throw Exception (msg.str().c_str(), ERANGE);
        }
        return (megs << 20);
    }

    GCache::GCache (std::string& fname, size_t megs)
        : mtx      (),
          fd       (fname, check_size(megs)),
          mmap     (fd),
          preamble (static_cast<char*>(mmap.value)),
          header   (reinterpret_cast<uint8_t*>(preamble + PREAMBLE_SIZE)),
          begin    (header + HEADER_SIZE),
          end      (reinterpret_cast<uint8_t*>(preamble + mmap.size)),
          size_cache (end - begin),
          version  (0)
    {
        first = begin;
        next  = begin;

        mallocs  = 0;
        reallocs = 0;
    }

    GCache::GCache (std::string& fname)
        : mtx      (),
          fd       (fname),
          mmap     (fd),
          preamble (static_cast<char*>(mmap.value)),
          header   (reinterpret_cast<uint8_t*>(preamble + PREAMBLE_SIZE)),
          begin    (header + HEADER_SIZE),
          end      (reinterpret_cast<uint8_t*>(preamble + mmap.size)),
          size_cache (end - begin),
          version  (0)
    {
        
    }

    GCache::~GCache ()
    {
        Lock lock(mtx);

        mmap.sync();
        mmap.unmap();
    }

    /*! prints object properties */
    void print (std::ostream& os)
    {
    }

    /* Memory allocation functions */
    void* malloc (size_t size)
    {
        return 0;
    }

    void  free (void* ptr)
    {
    }

    void* realloc (void* ptr, size_t size)
    {
        return 0;
    }

    /* Seqno related functions */

    /*!
     * Assign sequence number to buffer pointed to by ptr
     */
    void    seqno_assign  (void* ptr, int64_t seqno)
    {
    }

    /*!
     * Get the smallest seqno present in the cache.
     * Locks seqno from removal.
     */
    int64_t seqno_get_min ()
    {
        return -1;
    }

    /*!
     * Get pointer to buffer identified by seqno.
     * Moves lock to the given seqno.
     */
    void*   seqno_get_ptr (int64_t seqno)
    {
        return 0;
    }

    /*!
     * Releases any seqno locks present.
     */
    void    seqno_release ()
    {
    }
}
