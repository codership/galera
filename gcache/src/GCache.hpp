/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include <string>
#include <iostream>
#include <map>
#include <stdint.h>

#include <galerautils.hpp>

#include "FileDescriptor.hpp"
#include "MMap.hpp"

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

        gu::Mutex       mtx;
        gu::Cond        cond;

        FileDescriptor  fd;       // cache file descriptor

        MMap            mmap;

        bool            open;

        static size_t  const PREAMBLE_LEN;

        char*     const preamble; // ASCII text preamble
        int64_t*  const header;   // cache binary header
        size_t    const header_len;
        uint8_t*  const start;    // start of cache area
        uint8_t*  const end;      // first byte after cache area
        uint8_t*        first;    // pointer to the first (oldest) buffer
        uint8_t*        next;     // pointer to the next free space

        ssize_t   const size_cache;
        ssize_t         size_free;
        ssize_t         size_used;

        long long       mallocs;
        long long       reallocs;

        int64_t         seqno_locked;
        int64_t         seqno_min;
        int64_t         seqno_max;

        char     const  version;

        typedef std::map<int64_t, void*> seqno2ptr_t;
        seqno2ptr_t     seqno2ptr;

        void header_read();
        void header_write();
        void preamble_write();

        void reset_cache();
        void constructor_common();

        void* get_new_buffer (size_t size);

        inline void order_buffer   (void* ptr, int64_t seqno);
        inline void discard_buffer (void* bh);
        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache);
    };
}

#endif /* __GCACHE_H__ */
