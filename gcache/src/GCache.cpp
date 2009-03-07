/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

namespace gcache
{
    const size_t preamble_len = 1024;

    GCache::GCache (std::string& fname, size_t megs)
    {
    }

    GCache::GCache (std::string& fname)
    {
    }

    GCache::~GCache ()
    {
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
