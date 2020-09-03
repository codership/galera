/*
 * Copyright (C) 2011-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_test_utils.hpp"

#include "../gcs_group.hpp"
#include "../gcs_comp_msg.hpp"

#include "gu_uuid.h"

#include "gcs_test_utils.hpp"
#include "gcs_memb_test.hpp" // must be included last

using namespace gcs_test;

/* Thes test was specifically created to reproduce #465 */
START_TEST(gcs_memb_test_465)
{
    struct gt_group group;
    ck_assert(group.nodes_num == 0);

    struct gt_node nodes[GT_MAX_NODES];
    int i;
    ssize_t ret = 0;

    // initialize individual node structures
    for (i = 0; i < GT_MAX_NODES; i++) {
        int const str_len = 32;
        char name_str[str_len];
        char addr_str[str_len];

        sprintf(name_str, "node%d", i);
        sprintf(addr_str, "addr%d", i);
        nodes[i].group.init(name_str, addr_str, 1, 0, 0);
    }

    gcs_node_state_t node_state;

    // bootstrap the cluster
    group.add_node(&nodes[0], true);
    ck_assert(nodes[0].group.state() == GCS_GROUP_PRIMARY);
    node_state = nodes[0].state();
    ck_assert(node_state == GCS_NODE_STATE_JOINED);

    group.deliver_join_sync_msg(0, GCS_MSG_SYNC);
    node_state = nodes[0].state();
    ck_assert(node_state == GCS_NODE_STATE_SYNCED);

    group.add_node(&nodes[1], true);
    ck_assert(nodes[1].group.state() == GCS_GROUP_PRIMARY);
    node_state = nodes[1].state();
    ck_assert(node_state == GCS_NODE_STATE_PRIM); // need sst

    group.add_node(&nodes[2], true);
    ck_assert(nodes[2].group.state() == GCS_GROUP_PRIMARY);
    node_state = nodes[2].state();
    ck_assert(node_state == GCS_NODE_STATE_PRIM); // need sst

    ck_assert(group.verify_node_state_across(0, GCS_NODE_STATE_SYNCED));

    group.sst_start(2, nodes[0].group()->nodes[0].name);
    mark_point();
    group.deliver_join_sync_msg(0, GCS_MSG_JOIN); // end of donor SST
    group.deliver_join_sync_msg(0, GCS_MSG_SYNC); // donor synced
    group.deliver_join_sync_msg(2, GCS_MSG_SYNC); // joiner can't sync
    ck_assert(group.verify_node_state_across(2, GCS_NODE_STATE_JOINER));
    group.deliver_join_sync_msg(2, GCS_MSG_JOIN); // end of joiner SST
    group.deliver_join_sync_msg(2, GCS_MSG_SYNC); // joiner synced

    ck_assert(group.verify_node_state_across(0, GCS_NODE_STATE_SYNCED));
    ck_assert(group.verify_node_state_across(1, GCS_NODE_STATE_PRIM));
    ck_assert(group.verify_node_state_across(2, GCS_NODE_STATE_SYNCED));

    group.sst_start(1, nodes[0].group()->nodes[0].name);
    group.deliver_join_sync_msg(0, GCS_MSG_JOIN); // end of donor SST
    group.deliver_join_sync_msg(1, GCS_MSG_JOIN); // end of joiner SST

    struct gt_node* dropped = group.drop_node(1);
    ck_assert(NULL != dropped);

    /* After that, according to #465, node 1 shifted from SYNCED to PRIMARY */

    ck_assert(group.verify_node_state_across(1, GCS_NODE_STATE_SYNCED));
    struct gcs_act_rcvd rcvd;
    int                 proto_ver = -1;
    ret = gcs_group_act_conf(group.nodes[1]->group(), &rcvd, &proto_ver);
    struct gcs_act* const act(&rcvd.act);
    ck_assert_msg(ret > 0, "gcs_group_act_cnf() retruned %zd (%s)",
                  ret, strerror (-ret));
    ck_assert(ret == act->buf_len);
    ck_assert_msg(proto_ver == 1 /* current version */,
                  "proto_ver = %d", proto_ver);
    const gcs_act_cchange conf(act->buf, act->buf_len);
    int const my_idx(rcvd.id);
    ck_assert(my_idx == 1);

    group.deliver_join_sync_msg(0, GCS_MSG_SYNC); // donor synced
    ck_assert(group.verify_node_state_across(0, GCS_NODE_STATE_SYNCED));

    group.nodes[1]->group.gcache()->free(const_cast<void*>(act->buf));

    while (group.nodes_num)
    {
        struct gt_node* dropped = group.drop_node(0);
        ck_assert(NULL != dropped);
    }
    ck_assert(0 == group.nodes_num);
}
END_TEST

Suite *gcs_memb_suite(void)
{
    Suite *suite = suite_create("GCS membership changes");
    TCase *tcase = tcase_create("gcs_memb");

    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase, gcs_memb_test_465);
    return suite;
}
