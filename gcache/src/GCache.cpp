/*
 * Copyright (C) 2009-2025 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"
#include "gcache_bh.hpp"

#include <gu_logger.hpp>
#include "gu_thread_keys.hpp"

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

        gid            = gu::UUID();
        seqno_max      = SEQNO_NONE;
        seqno_released = SEQNO_NONE;
        seqno_low_     = SEQNO_NONE;
        seqno_locked   = SEQNO_MAX;
        seqno_locked_count = 0;

        seqno2ptr.clear(SEQNO_NONE);

#ifndef NDEBUG
        buf_tracker.clear();
#endif
    }

    static bool recover_rb(bool const encrypt, bool const recover)
    {
        if (encrypt)
        {
            if (recover)
            {
                log_info << "GCache recovery is not supported when encryption "
                    "is enabled. Recovery will be skipped.";
            }
            return false;
        }
        else
        {
            return recover;
        }
    }

    GCache::GCache (ProgressCallback*        pcb,
                    gu::Config&              cfg,
                    const std::string&       data_dir,
                    wsrep_encrypt_cb_t const encrypt_cb,
                    void*              const app_ctx)
        :
        config    (cfg),
        params    (config, data_dir),
        mtx       (gu::get_mutex_key(gu::GU_MUTEX_KEY_GCACHE)),
        seqno2ptr (SEQNO_NONE),
        gid       (),
        mem       (params.mem_size(), seqno2ptr, params.debug()),
        rb        (pcb, params.rb_name(), params.rb_size(), seqno2ptr, gid,
                   params.debug(), recover_rb(encrypt_cb, params.recover())),
        ps        (*this,
                   params.dir_name(),
                   encrypt_cb,
                   app_ctx,
                   params.keep_pages_size(),
                   params.page_size(),
                   params.keep_plaintext_size(),
                   params.debug(),
                   /* keep last page if PS is the only storage */
                   !((params.mem_size() + params.rb_size()) > 0)),
        mallocs   (0),
        reallocs  (0),
        frees     (0),
        seqno_max     (seqno2ptr.empty() ?
                       SEQNO_NONE : seqno2ptr.index_back()),
        seqno_released(seqno_max),
        seqno_low_    (SEQNO_NONE),
        seqno_locked  (SEQNO_MAX),
        seqno_locked_count(0),
        encrypt_cache (NULL != encrypt_cb)
#ifndef NDEBUG
        ,buf_tracker()
        ,in_dtor(false)
#endif
    {}

    GCache::~GCache ()
    {
        gu::Lock lock(mtx);
        log_debug << "\n" << "GCache mallocs : " << mallocs
                  << "\n" << "GCache reallocs: " << reallocs
                  << "\n" << "GCache frees   : " << frees;
#ifndef NDEBUG
        in_dtor = true;
#endif
    }

    /*! prints object properties */
    void GCache::print (std::ostream& os) {}

    void GCache::set_enc_key(const wsrep_enc_key_t& key)
    {
        const uint8_t* const ptr(static_cast<const uint8_t*>(key.ptr));
        Page::EncKey k(ptr, ptr + key.len);
        ps.set_enc_key(k);
    }

    std::string GCache::meta(const void* ptr)
    {
        std::ostringstream os;
        if (encrypt_cache)
        {
            ps.meta(ptr, os);
        }
        else
        {
            os << ptr2BH(ptr);
        }
        return os.str();
    }
}

#include "gcache.h"

gcache_t* gcache_create (gu_config_t* conf, const char* data_dir)
{
    /* this funciton is used only in tests */
    gcache::GCache* gc = new gcache::GCache (
        NULL, *reinterpret_cast<gu::Config*>(conf), data_dir);
    return reinterpret_cast<gcache_t*>(gc);
}

void gcache_destroy (gcache_t* gc)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    delete gcache;
}

void* gcache_malloc  (gcache_t* gc, int size, void** ptx)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->malloc (size, *ptx);
}

void* gcache_realloc (gcache_t* gc, void* ptr, int size, void** ptx)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->realloc (ptr, size, *ptx);
}

void  gcache_free    (gcache_t* gc, const void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->free (const_cast<void*>(ptr));
}

const void* gcache_get_ro_plaintext (gcache_t* gc, const void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->get_ro_plaintext (ptr);
}

void* gcache_get_rw_plaintext (gcache_t* gc, void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->get_rw_plaintext (ptr);
}

void gcache_drop_plaintext (gcache_t* gc, const void* ptr)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    gcache->drop_plaintext (ptr);
}

int64_t gcache_seqno_min (gcache_t* gc)
{
    gcache::GCache* gcache = reinterpret_cast<gcache::GCache*>(gc);
    return gcache->seqno_min ();
}
