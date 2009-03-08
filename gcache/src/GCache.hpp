/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include <string>
#include <iostream>
#include <tr1/cstdint>
#include <map>

#include "Mutex.hpp"
#include "FileDescriptor.hpp"

namespace gcache
{

    class GCache
    {

    public:

        /*!
         * Creates a new gcache using file fname of size megs megabytes.
         * If file already exists, it gets overwritten
         */
        GCache (std::string& fname, size_t megs);

        /*!
         * Creates a new gcache from existing file fname.
         */
        GCache (std::string& fname);

        virtual ~GCache();

        /*! prints object properties */
        void print (std::ostream& os);

        /* Memory allocation functions */
        void* malloc  (size_t size);
        void  free    (void* ptr);
        void* realloc (void* ptr, size_t size);

        /* Seqno related functions */

        /*!
         * Assign sequence number to buffer pointed to by ptr
         */
        void    seqno_assign  (void* ptr, int64_t seqno);

        /*!
         * Get the smallest seqno present in the cache.
         * Locks seqno from removal.
         */
        int64_t seqno_get_min ();

        /*!
         * Get pointer to buffer identified by seqno.
         * Moves lock to the given seqno.
         */
        void*   seqno_get_ptr (int64_t seqno);

        /*!
         * Releases any seqno locks present.
         */
        void    seqno_release ();

    private:

        Mutex           mtx;

        FileDescriptor  fd; // cache file descriptor

        size_t          size_total;
        size_t          size_free;
        size_t          size_used;

        long long       mallocs;
        long long       reallocs;

        void*           preamble; // text preamble
        void*           header;   // cache binary header
        void*           begin;    // beginning of cache area
        void*           end;      // first byte after cache area
        void*           first;    // pointer to the first (oldest) buffer
        void*           next;     // pointer to the next free space

        int64_t         seqno_locked;
        int64_t         seqno_min;
        int64_t         seqno_max;

        char            version;

        std::map<int64_t, void*> seqno2ptr;

        void init();

        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache);
    };
}

#endif /* __GCACHE_H__ */
