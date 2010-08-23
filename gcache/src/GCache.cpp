/*
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 */

#include <cerrno>
#include <unistd.h>
#include <galerautils.hpp>

#include "BufferHeader.hpp"
#include "GCache.hpp"

namespace gcache
{
    const size_t  GCache::PREAMBLE_LEN = 1024; // reserved for text preamble

    static size_t check_size (ssize_t s)
    {
        if (s < 0) gu_throw_error(EINVAL) << "Negative cache file size: " << s;

        return s + GCache::PREAMBLE_LEN;
    }

    void
    GCache::reset_cache()
    {
        first = start;
        next  = start;

        BH_clear (reinterpret_cast<BufferHeader*>(next));

        size_free = size_cache;
        size_used = 0;

        mallocs  = 0;
        reallocs = 0;

        seqno_locked = SEQNO_NONE;
        seqno_min    = SEQNO_NONE;
        seqno_max    = SEQNO_NONE;

        seqno2ptr.clear();
#ifndef NDEBUG
        buf_tracker.clear();
#endif
    }

    void
    GCache::constructor_common()
    {
        header_write ();
        preamble_write ();
    }

    GCache::GCache (gu::Config& cfg, const std::string& data_dir)
        :
        config    (cfg),
        params    (config, data_dir),
        mtx       (),
        cond      (),
        fd        (params.name, check_size(params.disk_size)),
        mmap      (fd),
        open      (true),
        preamble  (static_cast<char*>(mmap.ptr)),
        header    (reinterpret_cast<int64_t*>(preamble + PREAMBLE_LEN)),
        header_len(32),
        start     (reinterpret_cast<uint8_t*>(header + header_len)),
        end       (reinterpret_cast<uint8_t*>(preamble + mmap.size)),
        first     (start),
        next      (first),
        size_cache(end - start),
        size_free (size_cache),
        size_used (0),
        mallocs   (0),
        reallocs  (0),
        seqno_locked(SEQNO_NONE),
        seqno_min   (SEQNO_NONE),
        seqno_max   (SEQNO_NONE),
        version     (0),
        seqno2ptr   (),
        last_insert (seqno2ptr.begin())
#ifndef NDEBUG
        ,buf_tracker()
#endif
    {
        BH_clear (reinterpret_cast<BufferHeader*>(next));
        constructor_common ();
    }

    GCache::~GCache ()
    {
        gu::Lock lock(mtx);

        mmap.sync();

        open = false;
        header_write ();
        preamble_write ();

        mmap.sync();
        mmap.unmap();
    }

    /*! prints object properties */
    void print (std::ostream& os)
    {
    }
}

#include "gcache.h"

void* gcache_malloc  (gcache_t* gc, size_t size)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->malloc (size);
}

void  gcache_free    (gcache_t* gc, void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->free (ptr);
}

void* gcache_realloc (gcache_t* gc, void* ptr, size_t size)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->realloc (ptr, size);
}

void  gcache_seqno_init   (gcache_t* gc, int64_t seqno)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->seqno_init (seqno);
}

void  gcache_seqno_assign(gcache_t* gc, const void* ptr, int64_t seqno)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->seqno_assign (ptr, seqno);
}

