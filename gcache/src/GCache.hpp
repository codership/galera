/*
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_H__
#define __GCACHE_H__

#include "gcache_mem_store.hpp"
#include "gcache_rb_store.hpp"
#include "gcache_page_store.hpp"

#include <galerautils.hpp>

#include <string>
#include <iostream>
#include <map>
#ifndef NDEBUG
#include <set>
#endif
#include <stdint.h>

namespace gcache
{

    class GCache : public MemOps
    {

    public:

        /*!
         * Creates a new gcache file in "gcache.name" conf parameter or
         * in data_dir. If file already exists, it gets overwritten.
         */
        GCache (gu::Config& cfg, const std::string& data_dir);

        virtual ~GCache();

        /*! prints object properties */
        void print (std::ostream& os);

        /* Resets storage */
        void reset();

        /* Memory allocation functions */
        void* malloc  (ssize_t size) throw (gu::Exception);
        void  free    (void* ptr) throw ();
        void* realloc (void* ptr, ssize_t size) throw (gu::Exception);

        /* Seqno related functions */

        /*!
         * Reinitialize seqno sequence (after SST or such)
         * Clears cache from buffers with seqnos and sets seqno_min to seqno.
         */
        void    seqno_init    (int64_t seqno);

        /*!
         * Assign sequence number to buffer pointed to by ptr
         */
        void    seqno_assign  (const void* ptr, int64_t seqno);

        /*!
         * Get the smallest seqno present in the cache.
         * Locks seqno from removal.
         */
        int64_t seqno_get_min ();

        /*!
         * Get pointer to buffer identified by seqno.
         * Moves lock to the given seqno.
         */
        const void* seqno_get_ptr (int64_t seqno);

        /*!
         * Releases any seqno locks present.
         */
        void seqno_release ();

        void
        param_set (const std::string& key, const std::string& val)
            throw (gu::Exception, gu::NotFound);

        static size_t const PREAMBLE_LEN;

    private:

        gu::Config&     config;

        class Params
        {
        public:
            Params(gu::Config&, const std::string&) throw (gu::Exception);
            std::string const rb_name;
            std::string const dir_name;
            ssize_t           mem_size;
            ssize_t     const rb_size;
            ssize_t           page_size;
            ssize_t           keep_pages_size;
        }
            params;

        gu::Mutex       mtx;
        gu::Cond        cond;

        typedef std::map<int64_t, const void*> seqno2ptr_t;
        seqno2ptr_t     seqno2ptr;

        typedef seqno2ptr_t::iterator           seqno2ptr_iter_t;
        typedef std::pair<int64_t, const void*> seqno2ptr_pair_t;

        MemStore        mem;
        RingBuffer      rb;
        PageStore       ps;

        long long       mallocs;
        long long       reallocs;

        int64_t         seqno_locked;
        int64_t         seqno_min;
        int64_t         seqno_max;

#ifndef NDEBUG
        std::set<const void*> buf_tracker;
#endif

        void constructor_common();
        void discard_seqno (int64_t);

        // disable copying
        GCache (const GCache&);
        GCache& operator = (const GCache&);
    };
}

#endif /* __GCACHE_H__ */
