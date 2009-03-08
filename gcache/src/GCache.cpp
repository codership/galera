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
    const size_t  PREAMBLE_LEN = 1024;
    const int64_t SEQNO_NONE   = 0;

    GCache::GCache (std::string& fname, size_t megs)
        : mtx(), fd(fname, O_RDWR|O_CREAT|O_NOATIME, S_IRUSR|S_IWUSR)
    {
        if (megs != ((megs << 20) >> 20)) {
            std::ostringstream msg;
            msg << "Requested too high cache size: " << megs;
            throw Exception (msg.str().c_str(), ERANGE);
        }

        preamble = mmap (NULL, megs << 20, PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, fd.get(), 0);
        if (0 == preamble) {
            std::string msg = "mmap() failed: ";
            msg = msg + strerror(errno);
            throw Exception (msg.c_str(), errno);
        }

    }

    GCache::GCache (std::string& fname)
        : mtx(), fd(fname, O_RDWR|O_NOATIME, S_IRUSR|S_IWUSR)
    {

    }

    GCache::~GCache ()
    {
        Lock lock(mtx);
//        fsync (fd);
//        close (fd);
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
