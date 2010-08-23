/*`wq
 * Copyright (C) 2009-2010 Codership Oy <info@codership.com>
 */

#include "GCache.hpp"

static const std::string GCACHE_PARAMS_NAME       ("gcache.name");
static const std::string GCACHE_DEFAULT_BASENAME  ("galera.cache");
static const std::string GCACHE_PARAMS_RAM_SIZE   ("gcache.ram_size");
static const ssize_t     GCACHE_DEFAULT_RAM_SIZE  (10 << 20); // 10Mb
static const std::string GCACHE_PARAMS_DISK_SIZE  ("gcache.disk_size");
static const ssize_t     GCACHE_DEFAULT_DISK_SIZE (10 << 20);

static const std::string&
name_value (gu::Config& cfg, const std::string& data_dir)
    throw ()
{
    try
    {
        return cfg.get (GCACHE_PARAMS_NAME);
    }
    catch (gu::NotFound&)
    {
        if (data_dir.empty())
        {
            cfg.set (GCACHE_PARAMS_NAME, GCACHE_DEFAULT_BASENAME);
        }
        else
        {
            cfg.set (GCACHE_PARAMS_NAME,
                     data_dir + '/' + GCACHE_DEFAULT_BASENAME);
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
    name      (name_value (cfg, data_dir)),
    ram_size  (size_value (cfg,
                           GCACHE_PARAMS_RAM_SIZE, GCACHE_DEFAULT_RAM_SIZE)),
    disk_size (size_value (cfg,
                           GCACHE_PARAMS_DISK_SIZE, GCACHE_DEFAULT_DISK_SIZE))
{}

void
gcache::GCache::param_set (const std::string& key, const std::string& val)
    throw (gu::Exception, gu::NotFound)
{
    if (key == GCACHE_PARAMS_NAME)
    {
        gu_throw_error(EPERM) << "Can't change cache file name in runtime.";
    }
    else if (key == GCACHE_PARAMS_RAM_SIZE)
    {
        ssize_t tmp_size = gu::Config::from_config<ssize_t>(val);

        gu::Lock lock(mtx);
        /* locking here serves two purposes: ensures atomic setting of config
         * and params.ram_size and syncs with malloc() method */

        config.set<ssize_t>(key, tmp_size);
        params.ram_size = tmp_size;
    }
    else if (key == GCACHE_PARAMS_NAME)
    {
        gu_throw_error(EPERM) << "Can't change cache file size in runtime.";
    }
    else
    {
        throw gu::NotFound();
    }
}
