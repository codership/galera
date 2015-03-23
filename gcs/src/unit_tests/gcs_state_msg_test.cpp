// Copyright (C) 2007-2013 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#undef fail // needed by STL
#include <string.h>
#include "gcs_state_msg_test.hpp"
#define GCS_STATE_MSG_ACCESS
#include "../gcs_state_msg.hpp"

static int const QUORUM_VERSION = 3;

START_TEST (gcs_state_msg_test_basic)
{
    ssize_t send_len, ret;
    gu_uuid_t    state_uuid;
    gu_uuid_t    group_uuid;
    gu_uuid_t    prim_uuid;
    gcs_state_msg_t* send_state;
    gcs_state_msg_t* recv_state;

    gu_uuid_generate (&state_uuid, NULL, 0);
    gu_uuid_generate (&group_uuid, NULL, 0);
    gu_uuid_generate (&prim_uuid,  NULL, 0);

    send_state = gcs_state_msg_create (&state_uuid,
                                       &group_uuid,
                                       &prim_uuid,
                                       457,                // prim_seqno
                                       3465,               // last received seq.
                                       2345,               // last cached seq.
                                       5,                  // prim_joined
                                       GCS_NODE_STATE_JOINED,   // prim_state
                                       GCS_NODE_STATE_NON_PRIM, // current_state
                                       "My Name",          // name
                                       "192.168.0.1:2345", // inc_addr
                                       0,                  // gcs_proto_ver
                                       1,                  // repl_proto_ver
                                       1,                  // appl_proto_ver
                                       GCS_STATE_FREP      // flags
        );

    fail_if (NULL == send_state);

    send_len = gcs_state_msg_len (send_state);
    fail_if (send_len < 0, "gcs_state_msg_len() returned %zd (%s)",
             send_len, strerror (-send_len));
    {
        uint8_t send_buf[send_len];

        ret = gcs_state_msg_write (send_buf, send_state);
        fail_if (ret != send_len, "Return value does not match send_len: "
                 "expected %zd, got %zd", send_len, ret);

        recv_state = gcs_state_msg_read (send_buf, send_len);
        fail_if (NULL == recv_state);
    }

    fail_if (send_state->flags          != recv_state->flags);
    fail_if (send_state->gcs_proto_ver  != recv_state->gcs_proto_ver);
    fail_if (send_state->repl_proto_ver != recv_state->repl_proto_ver);
    fail_if (send_state->appl_proto_ver != recv_state->appl_proto_ver);
    fail_if (recv_state->appl_proto_ver != 1, "appl_proto_ver: %d",
             recv_state->appl_proto_ver);
    fail_if (send_state->received       != recv_state->received,
             "Last received seqno: sent %lld, recv %lld",
             send_state->received, recv_state->received);
    fail_if (send_state->cached         != recv_state->cached,
             "Last cached seqno: sent %lld, recv %lld",
             send_state->cached, recv_state->cached);
    fail_if (send_state->prim_seqno    != recv_state->prim_seqno);
    fail_if (send_state->current_state != recv_state->current_state);
    fail_if (send_state->prim_state    != recv_state->prim_state);
    fail_if (send_state->prim_joined   != recv_state->prim_joined);
    fail_if (gu_uuid_compare (&recv_state->state_uuid, &state_uuid));
    fail_if (gu_uuid_compare (&recv_state->group_uuid, &group_uuid));
    fail_if (gu_uuid_compare (&recv_state->prim_uuid,  &prim_uuid));
    fail_if (strcmp(send_state->name,     recv_state->name));
    fail_if (strcmp(send_state->inc_addr, recv_state->inc_addr));

    {
        size_t str_len = 1024;
        char   send_str[str_len];
        char   recv_str[str_len];

        fail_if (gcs_state_msg_snprintf (send_str, str_len, send_state) <= 0);
        fail_if (gcs_state_msg_snprintf (recv_str, str_len, recv_state) <= 0);
// no longer true fail_if (strncmp (send_str, recv_str, str_len));
    }

    gcs_state_msg_destroy (send_state);
    gcs_state_msg_destroy (recv_state);
}
END_TEST

START_TEST (gcs_state_msg_test_quorum_inherit)
{
    gcs_state_msg_t* st[3] = { NULL, };

    gu_uuid_t state_uuid;
    gu_uuid_t group1_uuid, group2_uuid;
    gu_uuid_t prim1_uuid, prim2_uuid;

    gu_uuid_generate (&state_uuid,  NULL, 0);
    gu_uuid_generate (&group1_uuid, NULL, 0);
    gu_uuid_generate (&group2_uuid, NULL, 0);
    gu_uuid_generate (&prim1_uuid,  NULL, 0);
    gu_uuid_generate (&prim2_uuid,  NULL, 0);

    gcs_seqno_t prim1_seqno = 123;
    gcs_seqno_t prim2_seqno = 834;

    gcs_seqno_t act1_seqno = 345;
    gcs_seqno_t act2_seqno = 239472508908LL;

    gcs_state_quorum_t quorum;

    mark_point();

    /* First just nodes from different groups and configurations, none JOINED */
    st[0] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno - 1, act2_seqno - 1, act2_seqno-1,
                                  5, GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node0", "",
                                  0, 1, 1, 0);
    fail_if(NULL == st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 1, 3,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0);
    fail_if(NULL == st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 2, 5,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node2", "",
                                  0, 1, 1, 1);
    fail_if(NULL == st[2]);

    gu_info ("                  Inherited 1");
    int ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                        sizeof(st)/sizeof(gcs_state_msg_t*),
                                        &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (false != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    fail_if (GCS_SEQNO_ILL != quorum.act_id);
    fail_if (GCS_SEQNO_ILL != quorum.conf_id);
    fail_if (-1 != quorum.gcs_proto_ver);
    fail_if (-1 != quorum.repl_proto_ver);
    fail_if (-1 != quorum.appl_proto_ver);

    /* now make node1 inherit PC */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 3, 3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_DONOR,
                                  "node1", "",
                                  0, 1, 0, 0);
    fail_if(NULL == st[1]);

    gu_info ("                  Inherited 2");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (true != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &group1_uuid));
    fail_if (act1_seqno  != quorum.act_id);
    fail_if (prim1_seqno != quorum.conf_id);
    fail_if (0 != quorum.gcs_proto_ver);
    fail_if (1 != quorum.repl_proto_ver);
    fail_if (0 != quorum.appl_proto_ver);

    /* now make node0 inherit PC (should yield conflicting uuids) */
    gcs_state_msg_destroy (st[0]);
    st[0] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno - 1, act2_seqno - 1, -1, 5,
                                  GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_SYNCED,
                                  "node0", "",
                                  0, 1, 1, 0);
    fail_if(NULL == st[0]);

    gu_info ("                  Inherited 3");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (false != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    fail_if (GCS_SEQNO_ILL != quorum.act_id);
    fail_if (GCS_SEQNO_ILL != quorum.conf_id);
    fail_if (-1 != quorum.gcs_proto_ver);
    fail_if (-1 != quorum.repl_proto_ver);
    fail_if (-1 != quorum.appl_proto_ver);

    /* now make node1 non-joined again: group2 should win */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno -3, 3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0);
    fail_if(NULL == st[1]);

    gu_info ("                  Inherited 4");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (true != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    fail_if (act2_seqno - 1 != quorum.act_id);
    fail_if (prim2_seqno - 1 != quorum.conf_id);
    fail_if (0 != quorum.gcs_proto_ver);
    fail_if (1 != quorum.repl_proto_ver);
    fail_if (0 != quorum.appl_proto_ver);

    /* now make node2 joined: it should become a representative */
    gcs_state_msg_destroy (st[2]);
    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 2, 5,
                                  GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_SYNCED,
                                  "node2", "",
                                  0, 1, 1, 0);
    fail_if(NULL == st[2]);

    gu_info ("                  Inherited 5");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (true != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    fail_if (act2_seqno != quorum.act_id);
    fail_if (prim2_seqno != quorum.conf_id);
    fail_if (0 != quorum.gcs_proto_ver);
    fail_if (1 != quorum.repl_proto_ver);
    fail_if (0 != quorum.appl_proto_ver);

    gcs_state_msg_destroy (st[0]);
    gcs_state_msg_destroy (st[1]);
    gcs_state_msg_destroy (st[2]);
}
END_TEST

START_TEST (gcs_state_msg_test_quorum_remerge)
{
    gcs_state_msg_t* st[3] = { NULL, };

    gu_uuid_t state_uuid;
    gu_uuid_t group1_uuid, group2_uuid;
    gu_uuid_t prim0_uuid, prim1_uuid, prim2_uuid;

    gu_uuid_generate (&state_uuid,  NULL, 0);
    gu_uuid_generate (&group1_uuid, NULL, 0);
    gu_uuid_generate (&group2_uuid, NULL, 0);
    gu_uuid_generate (&prim0_uuid,  NULL, 0);
    gu_uuid_generate (&prim1_uuid,  NULL, 0);
    gu_uuid_generate (&prim2_uuid,  NULL, 0);

    gcs_seqno_t prim1_seqno = 123;
    gcs_seqno_t prim2_seqno = 834;

    gcs_seqno_t act1_seqno = 345;
    gcs_seqno_t act2_seqno = 239472508908LL;

    gcs_state_quorum_t quorum;

    mark_point();

    /* First just nodes from different groups and configurations, none JOINED */
    st[0] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim0_uuid,
                                  prim2_seqno - 1, act2_seqno - 1,act2_seqno -2,
                                  5,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node0", "",
                                  0, 1, 1, 0);
    fail_if(NULL == st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 3, 3,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0);
    fail_if(NULL == st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, -1, 5,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node2", "",
                                  0, 1, 1, 1);
    fail_if(NULL == st[2]);

    gu_info ("                  Remerged 1");
    int ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                        sizeof(st)/sizeof(gcs_state_msg_t*),
                                        &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (false != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    fail_if (GCS_SEQNO_ILL != quorum.act_id);
    fail_if (GCS_SEQNO_ILL != quorum.conf_id);
    fail_if (-1 != quorum.gcs_proto_ver);
    fail_if (-1 != quorum.repl_proto_ver);
    fail_if (-1 != quorum.appl_proto_ver);

    /* Now make node0 to be joined at least once */
    gcs_state_msg_destroy (st[0]);
    st[0] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim0_uuid,
                                  prim2_seqno - 1, act2_seqno - 1, -1, 5,
                                  GCS_NODE_STATE_DONOR, GCS_NODE_STATE_NON_PRIM,
                                  "node0", "",
                                  0, 1, 1, 0);
    fail_if(NULL == st[0]);

    gu_info ("                  Remerged 2");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (true != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    fail_if (act2_seqno - 1 != quorum.act_id);
    fail_if (prim2_seqno - 1 != quorum.conf_id);
    fail_if (0 != quorum.gcs_proto_ver);
    fail_if (1 != quorum.repl_proto_ver);
    fail_if (0 != quorum.appl_proto_ver);

    /* Now make node2 to be joined too */
    gcs_state_msg_destroy (st[2]);
    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 3, 5,
                                  GCS_NODE_STATE_JOINED,GCS_NODE_STATE_NON_PRIM,
                                  "node2", "",
                                  0, 1, 1, 1);
    fail_if(NULL == st[2]);

    gu_info ("                  Remerged 3");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (true != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    fail_if (act2_seqno != quorum.act_id);
    fail_if (prim2_seqno != quorum.conf_id);
    fail_if (0 != quorum.gcs_proto_ver);
    fail_if (1 != quorum.repl_proto_ver);
    fail_if (0 != quorum.appl_proto_ver);

    /* now make node1 joined too: conflict */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno, 3,
                                  GCS_NODE_STATE_SYNCED,GCS_NODE_STATE_NON_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0);
    fail_if(NULL == st[1]);

    gu_info ("                  Remerged 4");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (false != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    fail_if (GCS_SEQNO_ILL != quorum.act_id);
    fail_if (GCS_SEQNO_ILL != quorum.conf_id);
    fail_if (-1 != quorum.gcs_proto_ver);
    fail_if (-1 != quorum.repl_proto_ver);
    fail_if (-1 != quorum.appl_proto_ver);

    /* now make node1 current joiner: should be ignored */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 2, 3,
                                  GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_JOINER,
                                  "node1", "",
                                  0, 1, 0, 0);
    fail_if(NULL == st[1]);

    gu_info ("                  Remerged 5");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    fail_if (0 != ret);
    fail_if (QUORUM_VERSION != quorum.version);
    fail_if (true != quorum.primary);
    fail_if (0 != gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    fail_if (act2_seqno != quorum.act_id);
    fail_if (prim2_seqno != quorum.conf_id);
    fail_if (0 != quorum.gcs_proto_ver);
    fail_if (1 != quorum.repl_proto_ver);
    fail_if (0 != quorum.appl_proto_ver);

    gcs_state_msg_destroy (st[0]);
    gcs_state_msg_destroy (st[1]);
    gcs_state_msg_destroy (st[2]);
}
END_TEST

START_TEST(gcs_state_msg_test_gh24)
{
    gcs_state_msg_t* st[7] = { NULL, };
    gu_uuid_t state_uuid, group_uuid;
    gu_uuid_generate(&state_uuid, NULL, 0);
    gu_uuid_generate(&group_uuid, NULL, 0);
    gu_uuid_t prim_uuid1, prim_uuid2;
    gu_uuid_generate(&prim_uuid1, NULL, 0);
    gu_uuid_generate(&prim_uuid2, NULL, 0);

    gcs_seqno_t prim_seqno1 = 37;
    int prim_joined1 = 3;
    gcs_seqno_t prim_seqno2 = 35;
    int prim_joined2 = 6;
    gcs_seqno_t received = 0;
    gcs_seqno_t cached = 0;

    gcs_state_quorum_t quorum;
    // first three are 35.
    st[0] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached, prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home0", "",
                                 0, 4, 2, 2);
    fail_unless(st[0] != 0);
    st[1] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached, prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home1", "",
                                 0, 4, 2, 2);
    fail_unless(st[1] != 0);
    st[2] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached, prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home2", "",
                                 0, 4, 2, 2);
    fail_unless(st[2] != 0);

    // last four are 37.
    st[3] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home3", "",
                                 0, 4, 2, 3);
    fail_unless(st[3] != 0);
    st[4] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home4", "",
                                 0, 4, 2, 2);
    fail_unless(st[4] != 0);
    st[5] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home5", "",
                                 0, 4, 2, 2);
    fail_unless(st[5] != 0);
    st[6] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_PRIM,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home6", "",
                                 0, 4, 2, 2);
    fail_unless(st[6] != 0);
    int ret = gcs_state_msg_get_quorum((const gcs_state_msg_t**)st, 7,
                                       &quorum);
    fail_unless(ret == 0);
    fail_unless(quorum.primary == true);
    fail_unless(quorum.conf_id == prim_seqno1);

    // // but we just have first five nodes, we don't have prim.
    // // because prim_joined=3 but there are only 2 joined nodes.
    // ret = gcs_state_msg_get_quorum((const gcs_state_msg_t**)st, 5,
    //                                &quorum);
    // fail_unless(ret == 0);
    // fail_unless(quorum.primary == false);

    for(int i=0;i<7;i++)
        gcs_state_msg_destroy(st[i]);
}
END_TEST

Suite *gcs_state_msg_suite(void)
{
  Suite *s  = suite_create("GCS state message");
  TCase *tc_basic   = tcase_create("gcs_state_msg_basic");
  TCase *tc_inherit = tcase_create("gcs_state_msg_inherit");
  TCase *tc_remerge = tcase_create("gcs_state_msg_remerge");

  suite_add_tcase (s, tc_basic);
  tcase_add_test  (tc_basic, gcs_state_msg_test_basic);

  suite_add_tcase (s, tc_inherit);
  tcase_add_test  (tc_inherit, gcs_state_msg_test_quorum_inherit);

  suite_add_tcase (s, tc_remerge);
  tcase_add_test  (tc_remerge, gcs_state_msg_test_quorum_remerge);
  tcase_add_test  (tc_remerge, gcs_state_msg_test_gh24);


  return s;
}
