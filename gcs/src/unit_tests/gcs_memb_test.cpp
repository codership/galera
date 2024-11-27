/*
 * Copyright (C) 2011-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_test_utils.hpp"

#include "../gcs_group.hpp"
#include "../gcs_comp_msg.hpp"

#include "gu_uuid.h"

#include "gcs_memb_test.hpp" // must be included last

using namespace gcs_test;

/**
 * Helper to bootstrap 3-node cluster:
 * 0 - SYNCED
 * 1 - PRIMARY
 * 2 - JOINED
 */
static void
bootstrap_3node_cluster(struct gt_group& group,
                        struct gt_node*  nodes,
                        bool const enc)
{
    ck_assert(group.nodes_num == 0);

    // initialize individual node structures
    for (int i = 0; i < GT_MAX_NODES; i++) {
        int const str_len = 32;
        char name_str[str_len];
        char addr_str[str_len];

        sprintf(name_str, "node%d", i);
        sprintf(addr_str, "addr%d", i);
        nodes[i].group.init(name_str, addr_str, enc, 1, 0, 0);
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

    ck_assert(group.verify_node_state_across(0, GCS_NODE_STATE_SYNCED));
    ck_assert(group.verify_node_state_across(1, GCS_NODE_STATE_PRIM));
    ck_assert(group.verify_node_state_across(2, GCS_NODE_STATE_JOINED));
}

static void
shutdown_cluster(struct gt_group& group)
{
    while (group.nodes_num)
    {
        struct gt_node* dropped = group.drop_node(0);
        ck_assert(NULL != dropped);
    }
    ck_assert(0 == group.nodes_num);
}

/* Thes test was specifically created to reproduce #465 */
static void
t465(bool const enc)
{
    struct gt_group group;
    struct gt_node  nodes[GT_MAX_NODES];

    bootstrap_3node_cluster(group, nodes, enc);

    group.deliver_join_sync_msg(2, GCS_MSG_SYNC); // joiner synced
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
    GcsGroup&           group1(group.nodes[1]->group);

    ssize_t ret = gcs_group_act_conf(group1.group(), &rcvd, &proto_ver);
    struct gcs_act* const act(&rcvd.act);
    ck_assert_msg(ret > 0, "gcs_group_act_cnf() retruned %zd (%s)",
                  ret, strerror (-ret));
    ck_assert(ret == act->buf_len);
    ck_assert_msg(proto_ver == 1 /* current version */,
                  "proto_ver = %d", proto_ver);
    const gcs_act_cchange conf(group1.gcache()->get_ro_plaintext(act->buf),
                               act->buf_len);
    group1.gcache()->free(const_cast<void*>(act->buf));
    int const my_idx(rcvd.id);
    ck_assert(my_idx == 1);

    group.deliver_join_sync_msg(0, GCS_MSG_SYNC); // donor synced
    ck_assert(group.verify_node_state_across(0, GCS_NODE_STATE_SYNCED));

    shutdown_cluster(group);
}

START_TEST(gcs_memb_test_465)
{
    t465(false);
}
END_TEST

START_TEST(gcs_memb_test_465E)
{
    t465(true);
}
END_TEST

static void
membership_service_test(bool const enc)
{
    struct gt_group group;
    struct gt_node  nodes[GT_MAX_NODES];

    bootstrap_3node_cluster(group, nodes, enc);
    /*
     * 0 - SYNCED
     * 1 - PRIMARY
     * 2 - JOINED
     */

    struct wsrep_membership *m0(NULL), *m1(NULL), *m2(NULL);
    struct gcs_group* const g0(nodes[0].group.group());
    struct gcs_group* const g1(nodes[1].group.group());
    struct gcs_group* const g2(nodes[2].group.group());

    try {
        gcs_group_get_membership(*g0, NULL, &m0);
        ck_abort_msg("Exception expected");
    } catch (gu::Exception& e) {
        ck_assert(NULL == m0);
    }
    gcs_group_get_membership(*g0, ::malloc, &m0); ck_assert(NULL != m0);
    gcs_group_get_membership(*g1, ::malloc, &m1); ck_assert(NULL != m1);
    gcs_group_get_membership(*g2, ::malloc, &m2); ck_assert(NULL != m2);

    ck_assert(3 == m0->num);

    size_t const m_size(sizeof(struct wsrep_membership) +
                        (m0->num - 1)*sizeof(struct wsrep_member_info_ext));

    ck_assert(0 == ::memcmp(m0, m1, m_size));
    ck_assert(0 == ::memcmp(m1, m2, m_size));

    ck_assert(0 ==
              ::memcmp(&m0->group_uuid, &g0->group_uuid, sizeof(wsrep_uuid_t)));
    ck_assert(m0->last_received == 0); // not supported yet
    ck_assert(m0->updated == g0->act_id_);
    ck_assert(m0->num == size_t(g2->num));

    ck_assert(m0->members[0].status == WSREP_MEMBER_SYNCED);
    ck_assert(m0->members[1].status == WSREP_MEMBER_UNDEFINED);
    ck_assert(m0->members[2].status == WSREP_MEMBER_JOINED);

    ::free(m0);
    ::free(m1);
    ::free(m2);

    // do SST from 0 tp 1 (to avoid hitting non-prim in shutdown_cluster() below
    group.sst_start(1, nodes[0].group()->nodes[0].name);

    gcs_group_get_membership(*g0, ::malloc, &m0); ck_assert(NULL != m0);

    ck_assert(m0->members[0].status == WSREP_MEMBER_DONOR);
    ck_assert(m0->members[1].status == WSREP_MEMBER_JOINER);
    ck_assert(m0->members[2].status == WSREP_MEMBER_JOINED);

    ::free(m0);

    group.deliver_join_sync_msg(0, GCS_MSG_JOIN); // end of donor SST
    group.deliver_join_sync_msg(1, GCS_MSG_JOIN); // end of joiner SST

    gcs_group_get_membership(*g0, ::malloc, &m0); ck_assert(NULL != m0);

    ck_assert(m0->members[0].status == WSREP_MEMBER_JOINED);
    ck_assert(m0->members[1].status == WSREP_MEMBER_JOINED);
    ck_assert(m0->members[2].status == WSREP_MEMBER_JOINED);

    ::free(m0);

    group.deliver_join_sync_msg(0, GCS_MSG_SYNC);
    group.deliver_join_sync_msg(1, GCS_MSG_SYNC);

    gcs_group_get_membership(*g0, ::malloc, &m0); ck_assert(NULL != m0);

    ck_assert(m0->members[0].status == WSREP_MEMBER_SYNCED);
    ck_assert(m0->members[1].status == WSREP_MEMBER_SYNCED);
    ck_assert(m0->members[2].status == WSREP_MEMBER_JOINED);

    ::free(m0);

    shutdown_cluster(group);
}

START_TEST(gcs_membership_service_test)
{
    membership_service_test(false);
}
END_TEST

START_TEST(gcs_membership_service_testE)
{
    membership_service_test(true);
}
END_TEST

Suite *gcs_memb_suite(void)
{
    Suite *suite = suite_create("GCS membership changes");

    TCase *tcase = tcase_create("gcs_memb");
    suite_add_tcase  (suite, tcase);
    tcase_add_test   (tcase, gcs_memb_test_465);
    tcase_add_test  (tcase, gcs_memb_test_465E);
    tcase_set_timeout(tcase, 30);

    tcase = tcase_create("membership_service");
    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase, gcs_membership_service_test);
    tcase_add_test  (tcase, gcs_membership_service_testE);

    return suite;
}
