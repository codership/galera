/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#include "../gcs_group.hpp"
#include "../../../gcache/src/GCache.hpp"

namespace gcs_test
{
    class InitConfig
    {
    public:
        InitConfig(gu::Config& cfg);
        InitConfig(gu::Config& cfg, const std::string& base_name);
    private:
        void common_ctor(gu::Config& cfg);
    };

    class GcsGroup
    {
    public:

        GcsGroup();
        GcsGroup(const std::string& node_id,
                 const std::string& inc_addr,
                 gcs_proto_t gver = 1, int pver = 2, int aver = 3);

        ~GcsGroup();

        void init(const char*  node_name,
                  const char*  inc_addr,
                  gcs_proto_t  gcs_proto_ver,
                  int          repl_proto_ver,
                  int          appl_proto_ver);

        struct gcs_group* group() { return &group_;  }
        struct gcs_group* operator()(){ return group();  }
        struct gcs_group* operator->(){ return &group_;  }

        gcache::GCache*   gcache() { return gcache_; }

    private:

        void common_ctor(const char* node_name, const char* inc_addr,
                         gcs_proto_t gver, int rver, int aver);

        void common_dtor();

        gu::Config         conf_;
        InitConfig         init_;
        gcache::GCache*    gcache_;
        struct gcs_group   group_;
        bool               initialized_;
    };
}
