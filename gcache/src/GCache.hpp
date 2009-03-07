/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include <string>
#include <iostream>
#include <tr1/cstdint>
#include <map>

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

        pthread_mutex_t lock;

        size_t          total_size;
        size_t          free_size;
        size_t          used_size;

        long long       mallocs;
        long long       reallocs;

        void*           begin;
        void*           end;
        void*           first;
        void*           last;

        int64_t         locked_seqno;

        char            version;

        std::map<int64_t, void*> seqno2ptr;
    };
}

#endif /* __GCACHE_H__ */
