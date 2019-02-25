/*
 * Copyright (C) 2009-2018 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

static const std::string GCACHE_PARAMS_DIR        ("gcache.dir");
static const std::string GCACHE_DEFAULT_DIR       ("");
static const std::string GCACHE_PARAMS_RB_NAME    ("gcache.name");
static const std::string GCACHE_DEFAULT_RB_NAME   ("galera.cache");
static const std::string GCACHE_PARAMS_MEM_SIZE   ("gcache.mem_size");
static const std::string GCACHE_DEFAULT_MEM_SIZE  ("0");
static const std::string GCACHE_PARAMS_RB_SIZE    ("gcache.size");
static const std::string GCACHE_DEFAULT_RB_SIZE   ("128M");
static const std::string GCACHE_PARAMS_PAGE_SIZE  ("gcache.page_size");
static const std::string GCACHE_DEFAULT_PAGE_SIZE (GCACHE_DEFAULT_RB_SIZE);
static const std::string GCACHE_PARAMS_KEEP_PAGES_SIZE("gcache.keep_pages_size");
static const std::string GCACHE_DEFAULT_KEEP_PAGES_SIZE("0");
#ifndef NDEBUG
static const std::string GCACHE_PARAMS_DEBUG      ("gcache.debug");
static const std::string GCACHE_DEFAULT_DEBUG     ("0");
#endif
static const std::string GCACHE_PARAMS_RECOVER    ("gcache.recover");
static const std::string GCACHE_DEFAULT_RECOVER   ("yes");

const std::string&
gcache::GCache::PARAMS_DIR                 (GCACHE_PARAMS_DIR);

void
gcache::GCache::Params::register_params(gu::Config& cfg)
{
    cfg.add(GCACHE_PARAMS_DIR,             GCACHE_DEFAULT_DIR);
    cfg.add(GCACHE_PARAMS_RB_NAME,         GCACHE_DEFAULT_RB_NAME);
    cfg.add(GCACHE_PARAMS_MEM_SIZE,        GCACHE_DEFAULT_MEM_SIZE);
    cfg.add(GCACHE_PARAMS_RB_SIZE,         GCACHE_DEFAULT_RB_SIZE);
    cfg.add(GCACHE_PARAMS_PAGE_SIZE,       GCACHE_DEFAULT_PAGE_SIZE);
    cfg.add(GCACHE_PARAMS_KEEP_PAGES_SIZE, GCACHE_DEFAULT_KEEP_PAGES_SIZE);
#ifndef NDEBUG
    cfg.add(GCACHE_PARAMS_DEBUG,           GCACHE_DEFAULT_DEBUG);
#endif
    cfg.add(GCACHE_PARAMS_RECOVER,         GCACHE_DEFAULT_RECOVER);
}

static const std::string
name_value (gu::Config& cfg, const std::string& data_dir)
{
    std::string dir(cfg.get(GCACHE_PARAMS_DIR));

    /* fallback to data_dir if gcache dir is not set */
    if (GCACHE_DEFAULT_DIR == dir && !data_dir.empty())
    {
        dir = data_dir;
        cfg.set (GCACHE_PARAMS_DIR, dir);
    }

    std::string rb_name(cfg.get (GCACHE_PARAMS_RB_NAME));

    /* prepend directory name to RB file name if the former is not empty and
     * the latter is not an absolute path */
    if ('/' != rb_name[0] && !dir.empty())
    {
        rb_name = dir + '/' + rb_name;
    }

    return rb_name;
}

gcache::GCache::Params::Params (gu::Config& cfg, const std::string& data_dir)
    :
    rb_name_  (name_value (cfg, data_dir)),
    dir_name_ (cfg.get(GCACHE_PARAMS_DIR)),
    mem_size_ (cfg.get<size_t>(GCACHE_PARAMS_MEM_SIZE)),
    rb_size_  (cfg.get<size_t>(GCACHE_PARAMS_RB_SIZE)),
    page_size_(cfg.get<size_t>(GCACHE_PARAMS_PAGE_SIZE)),
    keep_pages_size_(cfg.get<size_t>(GCACHE_PARAMS_KEEP_PAGES_SIZE)),
#ifndef NDEBUG
    debug_    (cfg.get<int>(GCACHE_PARAMS_DEBUG)),
#else
    debug_    (0),
#endif
    recover_  (cfg.get<bool>(GCACHE_PARAMS_RECOVER))
{}

void
gcache::GCache::param_set (const std::string& key, const std::string& val)
{
    if (key == GCACHE_PARAMS_RB_NAME)
    {
        gu_throw_error(EPERM) << "Can't change ring buffer name in runtime.";
    }
    else if (key == GCACHE_PARAMS_DIR)
    {
        gu_throw_error(EPERM) << "Can't change data dir in runtime.";
    }
    else if (key == GCACHE_PARAMS_MEM_SIZE)
    {
        size_t tmp_size = gu::Config::from_config<size_t>(val);

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<size_t>(key, tmp_size);
        params.mem_size(tmp_size);
        mem.set_max_size(params.mem_size());
    }
    else if (key == GCACHE_PARAMS_RB_SIZE)
    {
        gu_throw_error(EPERM) << "Can't change ring buffer size in runtime.";
    }
    else if (key == GCACHE_PARAMS_PAGE_SIZE)
    {
        size_t tmp_size = gu::Config::from_config<size_t>(val);

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<size_t>(key, tmp_size);
        params.page_size(tmp_size);
        ps.set_page_size(params.page_size());
    }
    else if (key == GCACHE_PARAMS_KEEP_PAGES_SIZE)
    {
        size_t tmp_size = gu::Config::from_config<size_t>(val);

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<size_t>(key, tmp_size);
        params.keep_pages_size(tmp_size);
        ps.set_keep_size(params.keep_pages_size());
    }
    else if (key == GCACHE_PARAMS_RECOVER)
    {
        gu_throw_error(EINVAL) << "'" << key
                               << "' has a meaning only on startup.";
    }
#ifndef NDEBUG
    else if (key == GCACHE_PARAMS_DEBUG)
    {
        int d = gu::Config::from_config<int>(val);

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<int>(key, d);
        params.debug(d);
        mem.set_debug(params.debug());
        rb.set_debug(params.debug());
        ps.set_debug(params.debug());
    }
#endif
    else
    {
        throw gu::NotFound();
    }
}
