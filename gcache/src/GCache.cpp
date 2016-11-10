/*
 * Copyright (C) 2009-2016 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"
#include "gcache_bh.hpp"

#include <gu_logger.hpp>

#include <cerrno>
#include <unistd.h>

namespace gcache
{
    void
    GCache::reset()
    {
        mem.reset();
        rb.reset();
        ps.reset();

        mallocs  = 0;
        reallocs = 0;
        frees    = 0;

        seqno_locked   = SEQNO_NONE;
        seqno_max      = SEQNO_NONE;
        seqno_released = SEQNO_NONE;
        gid            = gu::UUID();

        seqno2ptr.clear();

#ifndef NDEBUG
        buf_tracker.clear();
#endif
    }

    GCache::GCache (gu::Config& cfg, const std::string& data_dir)
        :
        config    (cfg),
        params    (config, data_dir),
        mtx       (),
        cond      (),
        seqno2ptr (),
        gid       (),
        mem       (params.mem_size(), seqno2ptr),
        rb        (params.rb_name(), params.rb_size(), seqno2ptr, gid,
                   params.recover()),
        ps        (params.dir_name(),
                   params.keep_pages_size(),
                   params.page_size(),
                   /* keep last page if PS is the only storage */
                   params.keep_pages_count() ?
                   params.keep_pages_count() :
                   !((params.mem_size() + params.rb_size()) > 0)),
        mallocs   (0),
        reallocs  (0),
        frees     (0),
        seqno_locked(SEQNO_NONE),
        seqno_max   (seqno2ptr.empty() ?
                     SEQNO_NONE : seqno2ptr.rbegin()->first),
        seqno_released(seqno_max)
#ifndef NDEBUG
        ,buf_tracker()
#endif
    {}

    GCache::~GCache ()
    {
        gu::Lock lock(mtx);
        log_debug << "\n" << "GCache mallocs : " << mallocs
                  << "\n" << "GCache reallocs: " << reallocs
                  << "\n" << "GCache frees   : " << frees;
    }

    size_t GCache::allocated_pool_size ()
    {
        gu::Lock lock(mtx);
        return mem.allocated_pool_size() +
               rb.allocated_pool_size() +
               ps.allocated_pool_size();
    }

    /*! prints object properties */
    void print (std::ostream& os) {}
}

#include "gcache.h"

gcache_t* gcache_create (gu_config_t* conf, const char* data_dir)
{
    gcache::GCache* gc = new gcache::GCache (
        *reinterpret_cast<gu::Config*>(conf), data_dir);
    return reinterpret_cast<gcache_t*>(gc);
}

void gcache_destroy (gcache_t* gc)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    delete gcache;
}

void* gcache_malloc  (gcache_t* gc, int size)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->malloc (size);
}

void  gcache_free    (gcache_t* gc, const void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->free (const_cast<void*>(ptr));
}

void* gcache_realloc (gcache_t* gc, void* ptr, int size)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->realloc (ptr, size);
}

int64_t gcache_seqno_min (gcache_t* gc)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->seqno_min ();
}

#if DEPRECATED
void  gcache_seqno_init   (gcache_t* gc, int64_t seqno)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->seqno_init (seqno);
}

void  gcache_seqno_assign(gcache_t* gc, const void* ptr, int64_t seqno)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->seqno_assign (ptr, seqno, -1, false);
}

void  gcache_seqno_release(gcache_t* gc, const void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->seqno_release ();
}
#endif

