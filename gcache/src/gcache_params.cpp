/*
 * Copyright (C) 2009-2011 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

static const std::string GCACHE_PARAMS_DIR        ("gcache.dir");
static const std::string GCACHE_PARAMS_NAME       ("gcache.name");
static const std::string GCACHE_DEFAULT_BASENAME  ("galera.cache");
static const std::string GCACHE_PARAMS_MEM_SIZE   ("gcache.mem_size");
static const ssize_t     GCACHE_DEFAULT_MEM_SIZE  (0);
static const std::string GCACHE_PARAMS_RB_SIZE    ("gcache.ring_buffer_size");
static const ssize_t     GCACHE_DEFAULT_RB_SIZE   (128 << 20); // 128Mb
static const std::string GCACHE_PARAMS_PAGE_SIZE  ("gcache.page_size");
static const ssize_t     GCACHE_DEFAULT_PAGE_SIZE (GCACHE_DEFAULT_RB_SIZE);
static const std::string GCACHE_PARAMS_KEEP_PAGES_SIZE("gcache.keep_pages_size");
static const ssize_t     GCACHE_DEFAULT_KEEP_PAGES_SIZE(0);

static const std::string&
name_value (gu::Config& cfg, const std::string& data_dir)
    throw ()
{
    std::string dir("");

    if (cfg.has(GCACHE_PARAMS_DIR))
    {
        dir = cfg.get(GCACHE_PARAMS_DIR);
    }
    else
    {
        if (!data_dir.empty()) dir = data_dir;

        cfg.set (GCACHE_PARAMS_DIR, dir);
    }

    try
    {
        return cfg.get (GCACHE_PARAMS_NAME);
    }
    catch (gu::NotFound&)
    {
        if (dir.empty())
        {
            cfg.set (GCACHE_PARAMS_NAME, GCACHE_DEFAULT_BASENAME);
        }
        else
        {
            cfg.set (GCACHE_PARAMS_NAME,
                     dir + '/' + GCACHE_DEFAULT_BASENAME);
        }
    }

    return cfg.get (GCACHE_PARAMS_NAME);
}

static ssize_t
size_value (gu::Config& cfg, const std::string& key, ssize_t def)
    throw (gu::Exception)
{
    try
    {
        return cfg.get<ssize_t> (key);
    }
    catch (gu::NotFound&)
    {
        cfg.set<ssize_t> (key, def);
    }

    return cfg.get<ssize_t> (key);
}

gcache::GCache::Params::Params (gu::Config& cfg, const std::string& data_dir)
    throw (gu::Exception)
    :
    rb_name   (name_value (cfg, data_dir)),
    dir_name  (cfg.get(GCACHE_PARAMS_DIR)),
    mem_size  (size_value (cfg,
                           GCACHE_PARAMS_MEM_SIZE, GCACHE_DEFAULT_MEM_SIZE)),
    rb_size   (size_value (cfg,
                           GCACHE_PARAMS_RB_SIZE, GCACHE_DEFAULT_RB_SIZE)),
    page_size (size_value (cfg,
                           GCACHE_PARAMS_PAGE_SIZE, GCACHE_DEFAULT_PAGE_SIZE)),
    keep_pages_size (size_value (cfg, GCACHE_PARAMS_KEEP_PAGES_SIZE,
                                      GCACHE_DEFAULT_KEEP_PAGES_SIZE))
{}

void
gcache::GCache::param_set (const std::string& key, const std::string& val)
    throw (gu::Exception, gu::NotFound)
{
    if (key == GCACHE_PARAMS_NAME)
    {
        gu_throw_error(EPERM) << "Can't change ring buffer name in runtime.";
    }
    else if (key == GCACHE_PARAMS_DIR)
    {
        gu_throw_error(EPERM) << "Can't change data dir in runtime.";
    }
    else if (key == GCACHE_PARAMS_MEM_SIZE)
    {
        ssize_t tmp_size = gu::Config::from_config<ssize_t>(val);

        if (tmp_size < 0)
            gu_throw_error(EINVAL) << "Negative memory buffer size";

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<ssize_t>(key, tmp_size);
        params.mem_size = tmp_size;
        mem.set_max_size (params.mem_size);
    }
    else if (key == GCACHE_PARAMS_RB_SIZE)
    {
        gu_throw_error(EPERM) << "Can't change ring buffer size in runtime.";
    }
    else if (key == GCACHE_PARAMS_PAGE_SIZE)
    {
        ssize_t tmp_size = gu::Config::from_config<ssize_t>(val);

        if (tmp_size < 0)
            gu_throw_error(EINVAL) << "Negative page buffer size";

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<ssize_t>(key, tmp_size);
        params.page_size = tmp_size;
        ps.set_page_size (params.page_size);
    }
    else if (key == GCACHE_PARAMS_KEEP_PAGES_SIZE)
    {
        ssize_t tmp_size = gu::Config::from_config<ssize_t>(val);

        if (tmp_size < 0)
            gu_throw_error(EINVAL) << "Negative keep pages size";

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<ssize_t>(key, tmp_size);
        params.keep_pages_size = tmp_size;
        ps.set_keep_size (params.keep_pages_size);
    }
    else
    {
        throw gu::NotFound();
    }
}
