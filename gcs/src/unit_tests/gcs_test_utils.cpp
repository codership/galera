/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#include "gcs_test_utils.hpp"

namespace gcs_test
{

void
InitConfig::common_ctor(gu::Config& cfg)
{
    gcache::GCache::register_params(cfg);
    gcs_register_params(reinterpret_cast<gu_config_t*>(&cfg));
}

InitConfig::InitConfig(gu::Config& cfg)
{
    common_ctor(cfg);
}

InitConfig::InitConfig(gu::Config& cfg, const std::string& base_name)
{
    common_ctor(cfg);
    std::string p("gcache.size=1M;gcache.name=");
    p += base_name;
    gu_trace(cfg.parse(p));
}

GcsGroup::GcsGroup() :
    conf_   (),
    init_   (conf_, "group"),
    gcache_ (NULL),
    group_  (),
    initialized_(false)
{}

void
GcsGroup::common_ctor(const char*  node_name,
                      const char*  inc_addr,
                      gcs_proto_t  gver,
                      int          rver,
                      int          aver)
{
    assert(NULL  == gcache_);
    assert(false == initialized_);

    conf_.set("gcache.name", std::string(node_name) + ".cache");
    gcache_ = new gcache::GCache(conf_, ".");

    int const err(gcs_group_init(&group_, &conf_,
                                 reinterpret_cast<gcache_t*>(gcache_),
                                 node_name, inc_addr, gver, rver, aver));
    if (err)
    {
        gu_throw_error(-err) << "GcsGroup init failed";
    }

    initialized_ = true;
}

GcsGroup::GcsGroup(const std::string& node_id,
                   const std::string& inc_addr,
                   gcs_proto_t gver, int rver, int aver) :
    conf_   (),
    init_   (conf_, "group"),
    gcache_ (NULL),
    group_  (),
    initialized_(false)
{
    common_ctor(node_id.c_str(), inc_addr.c_str(), gver, rver, aver);
}

void
GcsGroup::common_dtor()
{
    if (initialized_)
    {
        assert(NULL != gcache_);
        gcs_group_free(&group_);
        delete gcache_;

        std::string const gcache_name(conf_.get("gcache.name"));
        ::unlink(gcache_name.c_str());
    }
    else
    {
        assert(NULL == gcache_);
    }
}

void
GcsGroup::init(const char*  node_name,
               const char*  inc_addr,
               gcs_proto_t  gcs_proto_ver,
               int          repl_proto_ver,
               int          appl_proto_ver)
{
    common_dtor();
    initialized_ = false;
    gcache_ = NULL;
    common_ctor(node_name, inc_addr,gcs_proto_ver,repl_proto_ver,appl_proto_ver);
}

GcsGroup::~GcsGroup()
{
    common_dtor();
}

} // namespace
