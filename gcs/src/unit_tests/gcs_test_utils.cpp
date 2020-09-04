/*
 * Copyright (C) 2015-2020 Codership Oy <info@codership.com>
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

#include "../gcs_comp_msg.hpp"
#include <check.h>

gcs_seqno_t
gt_node::deliver_last_applied(int const from, gcs_seqno_t const la)
{
    gcs_seqno_t buf(gcs_seqno_htog(la));

    gcs_recv_msg_t const msg(&buf, sizeof(buf), sizeof(buf),
                             from,
                             GCS_MSG_LAST);
    return gcs_group_handle_last_msg(group(), &msg);
}

gt_node::gt_node(const char* const name, int const gcs_proto_ver)
    : group(),
      id()
{
    if (name)
    {
        snprintf(id, sizeof(id) - 1, "%s", name);
    }
    else
    {
        snprintf(id, sizeof(id) - 1, "%p", this);
    }

    id[sizeof(id) - 1] = '\0';

    int const str_len = sizeof(id) + 6;
    char name_str[str_len] = { '\0', };
    char addr_str[str_len] = { '\0', };
    snprintf(name_str, str_len - 1, "name:%s", id);
    snprintf(addr_str, str_len - 1, "addr:%s", id);

    group.init(name_str, addr_str, gcs_proto_ver, 0, 0);
}

gt_node::~gt_node()
{
}

/* delivers new component message to all memebers */
int
gt_group::deliver_component_msg(bool const prim)
{
    for (int i = 0; i < nodes_num; i++)
    {
        gcs_comp_msg_t* msg = gcs_comp_msg_new(prim, false, i, nodes_num, 0);
        if (msg)
        {
            for (int j = 0; j < nodes_num; j++) {
                const struct gt_node* const node(nodes[j]);
                long ret = gcs_comp_msg_add (msg, node->id, j);
                ck_assert_msg(j == ret, "Failed to add %d member: %ld (%s)",
                              j, ret, strerror(-ret));
                /* check that duplicate node ID is ignored */
                ret = gcs_comp_msg_add (msg, node->id, j);
                ck_assert_msg(ret < 0, "Added duplicate %d member", j);
            }

            /* check component message */
            ck_assert(i == gcs_comp_msg_self(msg));
            ck_assert(nodes_num == gcs_comp_msg_num(msg));

            for (int j = 0; j < nodes_num; j++) {
                const char* const src_id = nodes[j]->id;
                const char* const dst_id = gcs_comp_msg_member(msg, j)->id;
                ck_assert_msg(!strcmp(src_id, dst_id),
                              "%d node id %s, recorded in comp msg as %s",
                              j, src_id, dst_id);

                gcs_segment_t const dst_seg(gcs_comp_msg_member(msg,j)->segment);
                ck_assert_msg(j == dst_seg,
                              "%d node segment %d, recorded in comp msg as %d",
                              j, j, (int)dst_seg);
            }

            gcs_group_state_t ret =
                gcs_group_handle_comp_msg(nodes[i]->group(), msg);

            ck_assert(ret == GCS_GROUP_WAIT_STATE_UUID);

            gcs_comp_msg_delete (msg);

            /* check that uuids are properly recorded in internal structures */
            for (int j = 0; j < nodes_num; j++) {
                const char* src_id = nodes[j]->id;
                const char* dst_id = nodes[i]->group()->nodes[j].id;
                ck_assert_msg(!strcmp(src_id, dst_id),
                              "%d node id %s, recorded at node %d as %s",
                              j, src_id, i, dst_id);
            }
        }
        else {
            return -ENOMEM;
        }
    }

    return 0;
}

int
gt_group::perform_state_exchange()
{
    /* first deliver state uuid message */
    gu_uuid_t state_uuid;
    gu_uuid_generate (&state_uuid, NULL, 0);

    gcs_recv_msg_t uuid_msg(&state_uuid,
                            sizeof (state_uuid),
                            sizeof (state_uuid),
                            0,
                            GCS_MSG_STATE_UUID);

    gcs_group_state_t state;
    int i;
    for (i = 0; i < nodes_num; i++) {
        state = gcs_group_handle_uuid_msg (nodes[i]->group(),&uuid_msg);
        ck_assert_msg(state == GCS_GROUP_WAIT_STATE_MSG,
                      "Wrong group state after STATE_UUID message. "
                      "Expected: %d, got: %d", GCS_GROUP_WAIT_STATE_MSG, state);
    }

    /* complete state message exchange */
    for (i = 0; i < nodes_num; i++)
    {
        /* create state message from node i */
        gcs_state_msg_t* state =
            gcs_group_get_state (nodes[i]->group());
        ck_assert(NULL != state);

        ssize_t state_len = gcs_state_msg_len (state);
        uint8_t state_buf[state_len];
        gcs_state_msg_write (state_buf, state);

        gcs_recv_msg_t state_msg(state_buf,
                                 sizeof (state_buf),
                                 sizeof (state_buf),
                                 i,
                                 GCS_MSG_STATE_MSG);

        /* deliver to each of the nodes */
        int j;
        for (j = 0; j < nodes_num; j++) {
            gcs_group_state_t ret =
                gcs_group_handle_state_msg (nodes[j]->group(), &state_msg);

            if (nodes_num - 1 == i) { // a message from the last node
                ck_assert_msg(ret == GCS_GROUP_PRIMARY,
                              "Handling state msg failed: sender %d, "
                              "receiver %d", i, j);
            }
            else {
                ck_assert_msg(ret == GCS_GROUP_WAIT_STATE_MSG,
                              "Handling state msg failed: sender %d, "
                              "receiver %d", i, j);
            }
        }

        gcs_state_msg_destroy (state);
    }

    return 0;
}

int
gt_group::add_node(struct gt_node* node, bool const new_id)
{
    if (GT_MAX_NODES == nodes_num) return -ERANGE;

    if (new_id) {
        gu_uuid_t node_uuid;
        gu_uuid_generate (&node_uuid, NULL, 0);
        gu_uuid_print (&node_uuid, (char*)node->id, sizeof (node->id));
        gu_debug ("Node %d (%p) UUID: %s", nodes_num, node, node->id);
    }

    nodes[nodes_num] = node;
    nodes_num++;

    /* check that all node ids are different */
    int i;
    for (i = 0; i < nodes_num; i++) {
        int j;
        for (j = i+1; j < nodes_num; j++) {
            ck_assert_msg(strcmp(nodes[i]->id, nodes[j]->id),
                          "%d (%p) and %d (%p) have the same id: %s/%s",
                          i, nodes[i], j, nodes[j], nodes[i]->id, nodes[j]->id);
        }
    }

    /* deliver new component message to all nodes */
    int ret = deliver_component_msg(primary);
    ck_assert_msg(ret == 0, "Component message delivery failed: %d (%s)",
                  ret, strerror(-ret));

    /* deliver state exchange uuid */
    ret = perform_state_exchange();
    ck_assert_msg(ret == 0, "State exchange failed: %d (%s)",
                  ret, strerror(-ret));

    return 0;
}

/* NOTE: this function uses simplified and determinitstic algorithm where
 *       dropped node is always replaced by the last one in group.
 *       For our purposes (reproduction of #465) it fits perfectly. */
struct gt_node*
gt_group::drop_node(int const idx)
{
    ck_assert(idx >= 0);
    ck_assert(idx < nodes_num);

    struct gt_node* dropped = nodes[idx];

    nodes[idx] = nodes[nodes_num - 1];
    nodes[nodes_num - 1] = NULL;
    nodes_num--;

    if (nodes_num > 0) {
        deliver_component_msg(primary);
        perform_state_exchange();
    }

    return dropped;
}

/* for delivery of GCS_MSG_SYNC or GCS_MSG_JOIN msg*/
int
gt_group::deliver_join_sync_msg(int const src, gcs_msg_type_t const type)
{
    gcs_seqno_t    seqno = nodes[src]->group()->act_id_;
    gcs_recv_msg_t msg(&seqno,
                       sizeof (seqno),
                       sizeof (seqno),
                       src,
                       type);

    int ret = -1;
    int i;
    for (i = 0; i < nodes_num; i++) {
        gcs_group_t* const group = nodes[i]->group();
        switch (type) {
        case GCS_MSG_JOIN:
            ret = gcs_group_handle_join_msg(group, &msg);
            mark_point();
            if (i == src) {
                ck_assert_msg(ret == 1,
                              "%d failed to handle own JOIN message: %d (%s)",
                              i, ret, strerror (-ret));
            }
            else {
                ck_assert_msg(ret == 0,
                              "%d failed to handle other JOIN message: %d (%s)",
                              i, ret, strerror (-ret));
            }
            break;
        case GCS_MSG_SYNC:
            ret = gcs_group_handle_sync_msg(group, &msg);
            if (i == src) {
                ck_assert_msg(ret == 1 ||
                              group->nodes[src].status != GCS_NODE_STATE_JOINED,
                              "%d failed to handle own SYNC message: %d (%s)",
                              i, ret, strerror (-ret));
            }
            else {
                ck_assert_msg(ret == 0,
                              "%d failed to handle other SYNC message: %d (%s)",
                              i, ret, strerror (-ret));
            }
            break;
        default:
            ck_abort_msg("wrong message type: %d", type);
        }
    }

    return ret;
}

gcs_seqno_t
gt_group::deliver_last_applied(int const from, gcs_seqno_t const la)
{
    gcs_seqno_t res = GCS_SEQNO_ILL;

    if (nodes_num > 0) res = nodes[0]->deliver_last_applied(from, la);

    for (int i(1); i < nodes_num; ++i)
    {
        ck_assert(nodes[i]->deliver_last_applied(from, la) == res);
    }

    return res;
}

bool
gt_group::verify_node_state_across(int const idx, gcs_node_state_t const check)
const
{
    for (int i(0); i < nodes_num; i++)
    {
        gcs_node_state_t const state(nodes[i]->group()->nodes[idx].status);
        if (check != state) {
            gu_error("At node %d node's %d status is not %d, but %d",
                     i, idx, check, state);
            return false;
        }
    }

    return true;
}

/* start SST on behalf of node idx (joiner)
 * @return donor idx or negative error code */
int
gt_group::sst_start (int const joiner_idx,const char* donor_name)
{
    ck_assert(joiner_idx >= 0);
    ck_assert(joiner_idx <  nodes_num);

    ssize_t const req_len = strlen(donor_name) + 2;
    // leave one byte as sst request payload

    int donor_idx = -1;
    int i;
    for (i = 0; i < nodes_num; i++)
    {
        gcache::GCache* const gcache(nodes[i]->group.gcache());
        ck_assert(NULL != gcache);
        // sst request is expected to be dynamically allocated
        char* const req_buf = (char*)gcache->malloc(req_len);
        ck_assert(NULL != req_buf);
        sprintf (req_buf, "%s", donor_name);

        struct gcs_act_rcvd req(gcs_act(req_buf, req_len, GCS_ACT_STATE_REQ),
                                NULL,
                                GCS_SEQNO_ILL,
                                joiner_idx);

        int ret = gcs_group_handle_state_request(nodes[i]->group(), &req);

        if (ret < 0) { // don't fail here, we may want to test negatives
            gu_error (ret < 0, "Handling state request to '%s' failed: %d (%s)",
                      donor_name, ret, strerror (-ret));
            return ret;
        }

        if (i == joiner_idx) {
            ck_assert(ret == req_len);
            gcache->free(req_buf); // passed to joiner
        }
        else {
            if (ret > 0) {
                if (donor_idx < 0) {
                    ck_assert(req.id == i);
                    donor_idx = i;
                    gcache->free(req_buf); // passed to donor
                }
                else {
                    ck_abort_msg("More than one donor selected: %d, first "
                                 "donor: %d", i, donor_idx);
                }
            }
        }
    }

    ck_assert_msg(donor_idx >= 0, "Failed to select donor");

    for (i = 0; i < nodes_num; i++) {
        gcs_group_t* const group = nodes[i]->group();
        gcs_node_t* const donor = &group->nodes[donor_idx];
        gcs_node_state_t state = donor->status;
        ck_assert_msg(state == GCS_NODE_STATE_DONOR, "%d is not donor at %d",
                      donor_idx, i);
        int dc = donor->desync_count;
        ck_assert_msg(dc >= 1, "donor %d at %d has desync_count %d",
                      donor_idx, i, dc);

        gcs_node_t* const joiner = &group->nodes[joiner_idx];
        state = joiner->status;
        ck_assert_msg(state == GCS_NODE_STATE_JOINER, "%d is not joiner at %d",
                      joiner_idx, i);
        dc = joiner->desync_count;
        ck_assert_msg(dc == 0, "joiner %d at %d has desync_count %d",
                      donor_idx, i, dc);

        /* check that donor and joiner point at each other */
        ck_assert_msg(!memcmp(group->nodes[donor_idx].joiner,
                              group->nodes[joiner_idx].id,
                              GCS_COMP_MEMB_ID_MAX_LEN+1),
                      "Donor points at wrong joiner: expected %s, got %s",
                      group->nodes[joiner_idx].id,group->nodes[donor_idx].joiner);

        ck_assert_msg(!memcmp(group->nodes[joiner_idx].donor,
                              group->nodes[donor_idx].id,
                              GCS_COMP_MEMB_ID_MAX_LEN+1),
                      "Joiner points at wrong donor: expected %s, got %s",
                      group->nodes[donor_idx].id,group->nodes[joiner_idx].donor);
    }

    return donor_idx;
}

/* Finish SST on behalf of a node idx (joiner or donor) */
int
gt_group::sst_finish(int const idx)
{
    gcs_node_state_t node_state;

    deliver_join_sync_msg(idx, GCS_MSG_JOIN);
    node_state = nodes[idx]->state();
    ck_assert(node_state == GCS_NODE_STATE_JOINED);

    deliver_join_sync_msg(idx, GCS_MSG_SYNC);
    node_state = nodes[idx]->state();
    ck_assert(node_state == GCS_NODE_STATE_SYNCED);

    return 0;
}

int
gt_group::sync_node(int const joiner_idx)
{
    gcs_node_state_t const node_state(nodes[joiner_idx]->state());
    ck_assert(node_state == GCS_NODE_STATE_PRIM);

    // initiate SST
    int const donor_idx(sst_start(joiner_idx, ""));
    ck_assert(donor_idx >= 0);
    ck_assert(donor_idx != joiner_idx);

    // complete SST
    int err;
    err = sst_finish(donor_idx);
    ck_assert(0 == err);
    err = sst_finish(joiner_idx);
    ck_assert(0 == err);

    return 0;
}

gt_group::gt_group(int const num, int const gcs_proto_ver, bool const prim)
    : nodes(),
      nodes_num(0),
      primary(prim)
{
    if (num > 0)
    {
        for (int i = 0; i < num; ++i)
        {
            char name[32];
            sprintf(name, "%d", i);
            add_node(new gt_node(name, gcs_proto_ver), true);
            bool const prim_state(nodes[0]->group.state() == GCS_GROUP_PRIMARY);
            ck_assert(prim_state == prim);

            gcs_node_state_t node_state(nodes[i]->state());
            if (primary)
            {
                if (0 == i)
                {
                    ck_assert(node_state == GCS_NODE_STATE_JOINED);

                    deliver_join_sync_msg(0, GCS_MSG_SYNC);
                    node_state = nodes[0]->state();
                    ck_assert(node_state == GCS_NODE_STATE_SYNCED);
                }
                else
                {
                    ck_assert(node_state == GCS_NODE_STATE_PRIM);

                    // initiate SST
                    int const donor_idx(sst_start(i, ""));
                    ck_assert(donor_idx >= 0);
                    ck_assert(donor_idx != i);

                    // complete SST
                    int err;
                    err = sst_finish(donor_idx);
                    ck_assert(0 == err);
                    err = sst_finish(i);
                    ck_assert(0 == err);
                }
            }
            else
            {
                ck_assert(node_state == GCS_NODE_STATE_NON_PRIM);
            }
        }
    }

    ck_assert(num == nodes_num);
}

gt_group::~gt_group()
{
    while (nodes_num)
    {
        struct gt_node* const node(drop_node(0));
        ck_assert(node != NULL);
        delete node;
    }
}
