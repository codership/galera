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

    static void* mmap_fd (const FileDescriptor& fd, size_t length)
    {
        void* ret = mmap (NULL, length, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, fd.get(), 0);

        if (ret == MAP_FAILED) {
            int err = errno;
            std::string msg ("mmap() on '" + fd.get_name() + "' failed: " +
                             strerror(err));
            throw Exception (msg.c_str(), err);
        }

        log_debug << "Memory mapped: " << ret << " (" << length << " bytes)";

        return ret;
    }

    GCache::GCache (std::string& fname, size_t megs)
        : size_mmap  (check_size(megs)),
          mtx        (),
          fd         (fname, true)
    {
        preamble   = (char*)(mmap_fd (fd, size_mmap));
        header     = (uint8_t*)(preamble + PREAMBLE_SIZE);
        begin      = header + HEADER_SIZE;
        size_cache = size_mmap - PREAMBLE_SIZE - HEADER_SIZE;
        end        = begin + size_cache;

        first = begin;
        next  = begin;

        mallocs  = 0;
        reallocs = 0;
    }

    GCache::GCache (std::string& fname)
        : mtx        (),
          fd         (fname, false)
    {
        
    }

    static void munmap_fd (void* ptr, size_t length)
    {
        int err = munmap (ptr, length);
        if (err < 0) {
            err = errno;
            std::string msg ("munmap() failed: ");
            msg = msg + strerror(err);
            throw Exception (msg.c_str(), err);
        }

        log_debug << "Memory unmapped: " << ptr << " (" << length <<" bytes)";
    }

    GCache::~GCache ()
    {
        Lock lock(mtx);

        munmap_fd(preamble, size_mmap);
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
