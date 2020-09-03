/*
 * Copyright (C) 2015-2020 Codership Oy <info@codership.com>
 */

#ifndef __gcs_test_utils__
#define __gcs_test_utils__

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

        gu::Config&       config() { return conf_; }
        gcache::GCache*   gcache() { return gcache_; }

        gcs_group_state_t state() const { return group_.state; }

        gcs_node_state_t  node_state() const
        { return group_.nodes[group_.my_idx].status; }

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
} /* namespace gcs_test */

struct gt_node
{
    gcs_test::GcsGroup group;
    char id[GCS_COMP_MEMB_ID_MAX_LEN + 1]; /// ID assigned by the backend

    explicit gt_node(const char* name = NULL, int gcs_proto_ver = 0);
    ~gt_node();

    gcs_node_state_t state() const { return group.node_state(); }

    gcs_seqno_t deliver_last_applied(int from, gcs_seqno_t last_applied);

};

#define GT_MAX_NODES 10

struct gt_group
{
    struct gt_node* nodes[GT_MAX_NODES];
    int             nodes_num;
    bool            primary;

    explicit gt_group(int num = 0, int gcs_proto_ver = 0, bool prim = true);
    ~gt_group();

    /* deliver new component message to all memebers */
    int deliver_component_msg(bool prim);

    /* perform state exchange between the members */
    int perform_state_exchange();

    /* add node to group (deliver new component and perform state exchange)
     * @param new_id should node get new ID? */
    int add_node(struct gt_node*, bool new_id);

    /* NOTE: this function uses simplified and determinitstic algorithm where
     *       dropped node is always replaced by the last one in group.
     *       For our purposes (reproduction of #465) it fits perfectly.
     * @return dropped node handle */
    struct gt_node* drop_node(int idx);

    /* deliver GCS_MSG_SYNC or GCS_MSG_JOIN msg*/
    int deliver_join_sync_msg (int src_idx, gcs_msg_type_t type);

    /* deliver last_applied message from node from */
    gcs_seqno_t deliver_last_applied(int from, gcs_seqno_t last_applied);

    /* @return true if all nodes in the group see node @param idx with a state
     * @param check */
    bool verify_node_state_across(int idx, gcs_node_state_t check) const;

    /* start SST on behalf of node idx (joiner)
     * @return donor idx or negative error code */
    int sst_start (int joiner_idx, const char* donor_name);

    /* Finish SST on behalf of a node idx (joiner or donor) */
    int sst_finish(int idx);

    /* join and sync added node (sst_start() + sst_finish()) */
    int sync_node(int joiner_idx);
};

#endif /* __gu_test_utils__ */
