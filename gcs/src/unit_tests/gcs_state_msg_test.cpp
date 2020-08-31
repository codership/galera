// Copyright (C) 2007-2020 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include <string.h>
#include "gcs_state_msg_test.hpp"
#define GCS_STATE_MSG_ACCESS
#include "../gcs_state_msg.hpp"

#include "gu_inttypes.hpp"

static int const QUORUM_VERSION = 6;

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
                                       2,                  // appl_proto_ver
                                       0,                  // prim_gcs_ver
                                       0,                  // prim_repl_ver
                                       1,                  // prim_appl_ver
                                       0,                  // desync_count
                                       GCS_STATE_FREP      // flags
        );

    ck_assert(NULL != send_state);

    send_len = gcs_state_msg_len (send_state);
    ck_assert_msg(send_len >= 0, "gcs_state_msg_len() returned %zd (%s)",
                  send_len, strerror (-send_len));
    {
        uint8_t send_buf[send_len];

        ret = gcs_state_msg_write (send_buf, send_state);
        ck_assert_msg(ret == send_len, "Return value does not match send_len: "
                      "expected %zd, got %zd", send_len, ret);

        recv_state = gcs_state_msg_read (send_buf, send_len);
        ck_assert(NULL != recv_state);
    }

    ck_assert(send_state->flags          == recv_state->flags);
    ck_assert(send_state->gcs_proto_ver  == recv_state->gcs_proto_ver);
    ck_assert(send_state->repl_proto_ver == recv_state->repl_proto_ver);
    ck_assert_msg(recv_state->repl_proto_ver == 1, "repl_proto_ver: %d",
                  recv_state->repl_proto_ver);
    ck_assert(send_state->appl_proto_ver == recv_state->appl_proto_ver);
    ck_assert_msg(recv_state->appl_proto_ver == 2, "appl_proto_ver: %d",
                  recv_state->appl_proto_ver);
    ck_assert(send_state->prim_gcs_ver  == recv_state->prim_gcs_ver);
    ck_assert(send_state->prim_repl_ver == recv_state->prim_repl_ver);
    ck_assert(send_state->prim_appl_ver == recv_state->prim_appl_ver);
    ck_assert_msg(recv_state->prim_appl_ver == 1, "prim_appl_ver: %d",
                  recv_state->prim_appl_ver);
    ck_assert_msg(send_state->received       == recv_state->received,
                  "Last received seqno: sent %" PRId64 " , recv %" PRId64,
                  send_state->received, recv_state->received);
    ck_assert_msg(send_state->cached         == recv_state->cached,
                  "Last cached seqno: sent %" PRId64 ", recv %" PRId64,
                  send_state->cached, recv_state->cached);
    ck_assert(send_state->prim_seqno    == recv_state->prim_seqno);
    ck_assert(send_state->current_state == recv_state->current_state);
    ck_assert(send_state->prim_state    == recv_state->prim_state);
    ck_assert(send_state->prim_joined   == recv_state->prim_joined);
    ck_assert(!gu_uuid_compare(&recv_state->state_uuid, &state_uuid));
    ck_assert(!gu_uuid_compare(&recv_state->group_uuid, &group_uuid));
    ck_assert(!gu_uuid_compare(&recv_state->prim_uuid,  &prim_uuid));
    ck_assert(!strcmp(send_state->name,     recv_state->name));
    ck_assert(!strcmp(send_state->inc_addr, recv_state->inc_addr));

    {
        size_t str_len = 1024;
        char   send_str[str_len];
        char   recv_str[str_len];

        ck_assert(gcs_state_msg_snprintf(send_str, str_len, send_state) > 0);
        ck_assert(gcs_state_msg_snprintf(recv_str, str_len, recv_state) > 0);
// no longer true ck_assert(strncmp (send_str, recv_str, str_len));
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
                                  0, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 1, 3,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 2, 5,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node2", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 1);
    ck_assert(NULL != st[2]);

    gu_info ("                  Inherited 1");
    int ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                        sizeof(st)/sizeof(gcs_state_msg_t*),
                                        &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(false == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    ck_assert(GCS_SEQNO_ILL == quorum.act_id);
    ck_assert(GCS_SEQNO_ILL == quorum.conf_id);
    ck_assert(-1 == quorum.gcs_proto_ver);
    ck_assert(-1 == quorum.repl_proto_ver);
    ck_assert(-1 == quorum.appl_proto_ver);

    /* now make node1 inherit PC */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 3, 3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_DONOR,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    gu_info ("                  Inherited 2");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group1_uuid));
    ck_assert(act1_seqno  == quorum.act_id);
    ck_assert(prim1_seqno == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(0 == quorum.appl_proto_ver);

    /* now make node0 inherit PC (should yield conflicting uuids) */
    gcs_state_msg_destroy (st[0]);
    st[0] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno - 1, act2_seqno - 1, -1, 5,
                                  GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_SYNCED,
                                  "node0", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    gu_info ("                  Inherited 3");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(false == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    ck_assert(GCS_SEQNO_ILL == quorum.act_id);
    ck_assert(GCS_SEQNO_ILL == quorum.conf_id);
    ck_assert(-1 == quorum.gcs_proto_ver);
    ck_assert(-1 == quorum.repl_proto_ver);
    ck_assert(-1 == quorum.appl_proto_ver);

    /* now make node1 non-joined again: group2 should win */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno -3, 3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    gu_info ("                  Inherited 4");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    ck_assert(act2_seqno - 1 == quorum.act_id);
    ck_assert(prim2_seqno - 1 == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(0 == quorum.appl_proto_ver);

    /* now make node2 joined: it should become a representative */
    gcs_state_msg_destroy (st[2]);
    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 2, 5,
                                  GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_SYNCED,
                                  "node2", "",
                                  0, 1, 1, 0, 1, 0,
                                  0, 0);
    ck_assert(NULL != st[2]);

    gu_info ("                  Inherited 5");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    ck_assert(act2_seqno == quorum.act_id);
    ck_assert(prim2_seqno == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(0 == quorum.appl_proto_ver);

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
                                  0, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 3, 3,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, -1, 5,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node2", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 1);
    ck_assert(NULL != st[2]);

    gu_info ("                  Remerged 1");
    int ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                        sizeof(st)/sizeof(gcs_state_msg_t*),
                                        &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(false == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    ck_assert(GCS_SEQNO_ILL == quorum.act_id);
    ck_assert(GCS_SEQNO_ILL == quorum.conf_id);
    ck_assert(-1 == quorum.gcs_proto_ver);
    ck_assert(-1 == quorum.repl_proto_ver);
    ck_assert(-1 == quorum.appl_proto_ver);

    /* Now make node0 to be joined at least once */
    gcs_state_msg_destroy (st[0]);
    st[0] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim0_uuid,
                                  prim2_seqno - 1, act2_seqno - 1, -1, 5,
                                  GCS_NODE_STATE_DONOR, GCS_NODE_STATE_NON_PRIM,
                                  "node0", "",
                                  0, 1, 1, 0, 0, 0,
                                  3, 0);
    ck_assert(NULL != st[0]);
    ck_assert(3 == gcs_state_msg_get_desync_count(st[0]));

    gu_info ("                  Remerged 2");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    ck_assert(act2_seqno - 1 == quorum.act_id);
    ck_assert(prim2_seqno - 1 == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(0 == quorum.appl_proto_ver);

    /* Now make node2 to be joined too */
    gcs_state_msg_destroy (st[2]);
    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 3, 5,
                                  GCS_NODE_STATE_JOINED,GCS_NODE_STATE_NON_PRIM,
                                  "node2", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 1);
    ck_assert(NULL != st[2]);

    gu_info ("                  Remerged 3");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    ck_assert(act2_seqno == quorum.act_id);
    ck_assert(prim2_seqno == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(0 == quorum.appl_proto_ver);

    /* now make node1 joined too: conflict */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno, 3,
                                  GCS_NODE_STATE_SYNCED,GCS_NODE_STATE_NON_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    gu_info ("                  Remerged 4");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(false == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &GU_UUID_NIL));
    ck_assert(GCS_SEQNO_ILL == quorum.act_id);
    ck_assert(GCS_SEQNO_ILL == quorum.conf_id);
    ck_assert(-1 == quorum.gcs_proto_ver);
    ck_assert(-1 == quorum.repl_proto_ver);
    ck_assert(-1 == quorum.appl_proto_ver);

    /* now make node1 current joiner: should be ignored */
    gcs_state_msg_destroy (st[1]);
    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 2, 3,
                                  GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_JOINER,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    gu_info ("                  Remerged 5");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group2_uuid));
    ck_assert(act2_seqno == quorum.act_id);
    ck_assert(prim2_seqno == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(0 == quorum.appl_proto_ver);

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
                                 0, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[0] != 0);
    st[1] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached, prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home1", "",
                                 0, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[1] != 0);
    st[2] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached, prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home2", "",
                                 0, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[2] != 0);

    // last four are 37.
    st[3] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home3", "",
                                 0, 4, 2, 0, 0, 0,
                                 0, 3);
    ck_assert(st[3] != 0);
    st[4] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home4", "",
                                 0, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[4] != 0);
    st[5] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home5", "",
                                 0, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[5] != 0);
    st[6] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached, prim_joined1,
                                 GCS_NODE_STATE_PRIM,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home6", "",
                                 0, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[6] != 0);
    int ret = gcs_state_msg_get_quorum((const gcs_state_msg_t**)st, 7,
                                       &quorum);
    ck_assert(ret == 0);
    ck_assert(quorum.primary == true);
    ck_assert(quorum.conf_id == prim_seqno1);

    // // but we just have first five nodes, we don't have prim.
    // // because prim_joined=3 but there are only 2 joined nodes.
    // ret = gcs_state_msg_get_quorum((const gcs_state_msg_t**)st, 5,
    //                                &quorum);
    // ck_assert(ret == 0);
    // ck_assert(quorum.primary == false);

    for(int i=0;i<7;i++)
        gcs_state_msg_destroy(st[i]);
}
END_TEST

/* This test is to test that protocol downgrade is disabled with state
 * excahnge >= v6 */
START_TEST (gcs_state_msg_test_v6_upgrade)
{
    gcs_state_msg_t* st[3] = { NULL, };

    gu_uuid_t state_uuid;
    gu_uuid_t group_uuid;
    gu_uuid_t prim_uuid;

    gu_uuid_generate (&state_uuid,  NULL, 0);
    gu_uuid_generate (&group_uuid, NULL, 0);
    gu_uuid_generate (&prim_uuid,  NULL, 0);

    gcs_seqno_t prim_seqno = 123;
    gcs_seqno_t act_seqno  = 345;

    gcs_state_quorum_t quorum;

    mark_point();

    /* Start with "heterogeneous" PC, where node2 is a v4 node */
    st[0] = gcs_state_msg_create (&state_uuid, &group_uuid, &prim_uuid,
                                  prim_seqno - 1, act_seqno - 1, act_seqno - 1,
                                  3,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node0", "",
                                  4, 4, 4, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group_uuid, &prim_uuid,
                                  prim_seqno, act_seqno, act_seqno - 3,
                                  3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_JOINED,
                                  "node1", "",
                                  3, 3, 3, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group_uuid, &prim_uuid,
                                  prim_seqno, act_seqno, act_seqno - 3,
                                  3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_JOINED,
                                  "node2", "",
                                  1, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[2]);
    st[2]->version = 4;

    gu_info ("                  proto_ver I");
    int
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(4 == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(1 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(1 == quorum.appl_proto_ver);

#define UPDATE_STATE_MSG(x)                       \
    st[x]->prim_seqno = prim_seqno;               \
    st[x]->received   = act_seqno;                \
    st[x]->prim_gcs_ver  = quorum.gcs_proto_ver;  \
    st[x]->prim_repl_ver = quorum.repl_proto_ver; \
    st[x]->prim_appl_ver = quorum.appl_proto_ver;

    /* disconnect node2: protocol versions should go up (also bump seqnos) */
    prim_seqno++;
    act_seqno++;
    UPDATE_STATE_MSG(0);
    UPDATE_STATE_MSG(1);
    gu_info ("                  proto_ver II");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    2,
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(3 == quorum.gcs_proto_ver);
    ck_assert(3 == quorum.repl_proto_ver);
    ck_assert(3 == quorum.appl_proto_ver);

    /* reconnect node2: protocol versions should go down for backward
     * compatibility */
    prim_seqno++;
    act_seqno++;
    UPDATE_STATE_MSG(0);
    UPDATE_STATE_MSG(1);
    gu_info ("                  proto_ver III");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    3,
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(4 == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(1 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(1 == quorum.appl_proto_ver);


    /* disconnect node2 */
    prim_seqno++;
    act_seqno++;
    UPDATE_STATE_MSG(0);
    UPDATE_STATE_MSG(1);
    gu_info ("                  proto_ver IV");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    2,
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(3 == quorum.gcs_proto_ver);
    ck_assert(3 == quorum.repl_proto_ver);
    ck_assert(3 == quorum.appl_proto_ver);

    /* upgrade node2 */
    st[2]->version = QUORUM_VERSION;
    st[2]->gcs_proto_ver  = 2;
    st[2]->repl_proto_ver = 2;
    st[2]->appl_proto_ver = 2;

    /* reconnect node2: this time protocol versions should stay */
    prim_seqno++;
    act_seqno++;
    UPDATE_STATE_MSG(0);
    UPDATE_STATE_MSG(1);
    gu_info ("                  proto_ver V");
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    3,
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(QUORUM_VERSION == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(3 == quorum.gcs_proto_ver);
    ck_assert(3 == quorum.repl_proto_ver);
    ck_assert(3 == quorum.appl_proto_ver);

    gcs_state_msg_destroy (st[0]);
    gcs_state_msg_destroy (st[1]);
    gcs_state_msg_destroy (st[2]);
#undef UPDATE_STATE_MSG
}
END_TEST

Suite *gcs_state_msg_suite(void)
{
  Suite *s  = suite_create("GCS state message");
  TCase *tc_basic   = tcase_create("gcs_state_msg_basic");
  TCase *tc_inherit = tcase_create("gcs_state_msg_inherit");
  TCase *tc_remerge = tcase_create("gcs_state_msg_remerge");
  TCase *tc_proto_ver = tcase_create("gcs_state_msg_proto_ver");

  suite_add_tcase (s, tc_basic);
  tcase_add_test  (tc_basic, gcs_state_msg_test_basic);

  suite_add_tcase (s, tc_inherit);
  tcase_add_test  (tc_inherit, gcs_state_msg_test_quorum_inherit);

  suite_add_tcase (s, tc_remerge);
  tcase_add_test  (tc_remerge, gcs_state_msg_test_quorum_remerge);
  tcase_add_test  (tc_remerge, gcs_state_msg_test_gh24);

  suite_add_tcase (s, tc_proto_ver);
  tcase_add_test  (tc_proto_ver, gcs_state_msg_test_v6_upgrade);

  return s;
}
