// Copyright (C) 2007-2020 Codership Oy <info@codership.com>

// $Id$

#define GCS_STATE_MSG_ACCESS
#include "../gcs_state_msg.hpp"

#include "gcs_state_msg_test.hpp" // must be included last
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

    gcs_seqno_t const prim_seqno(457);
    gcs_seqno_t const received(3456);
    gcs_seqno_t const cached(2345);
    gcs_seqno_t const last_applied(3450);
    gcs_seqno_t const vote_seqno(3449);
    int64_t     const vote_res(0x1244567879012345ULL);
    int         const vote_policy(0);
    int         const prim_joined(5);
    gcs_node_state_t const prim_state(GCS_NODE_STATE_JOINED);
    gcs_node_state_t const current_state(GCS_NODE_STATE_NON_PRIM);
    const char* const name("MyName");
    const char* const inc_addr("192.168.0.1:2345");
    int         const gcs_proto_ver(2);
    int         const repl_proto_ver(1);
    int         const appl_proto_ver(2);
    int         const prim_gcs_ver(0);
    int         const prim_repl_ver(1);
    int         const prim_appl_ver(1);
    int         const desync_count(0);
    int         const flags(GCS_STATE_FREP);

    send_state = gcs_state_msg_create (&state_uuid,
                                       &group_uuid,
                                       &prim_uuid,
                                       prim_seqno,
                                       received,      // last received seqno
                                       cached,        // last cached seqno
                                       last_applied,  // last applied
                                       vote_seqno,    // last vote seqno
                                       vote_res,      // last vote result
                                       vote_policy,   // voting protocol
                                       prim_joined,   // prim_joined
                                       prim_state,    // prim_state
                                       current_state,  // current_state
                                       name,           // name
                                       inc_addr,       // inc_addr
                                       gcs_proto_ver,  // gcs_proto_ver
                                       repl_proto_ver, // repl_proto_ver
                                       appl_proto_ver, // appl_proto_ver
                                       prim_gcs_ver,   // prim_gcs_ver
                                       prim_repl_ver,  // prim_repl_ver
                                       prim_appl_ver,  // prim_appl_ver
                                       desync_count,   // desync_count
                                       flags           // flags
        );

    ck_assert(NULL != send_state);

    ck_assert(send_state->flags          == flags);
    ck_assert(send_state->gcs_proto_ver  == gcs_proto_ver);
    ck_assert(send_state->repl_proto_ver == repl_proto_ver);
    ck_assert(send_state->appl_proto_ver == appl_proto_ver);
    ck_assert_msg(send_state->received   == received,
                  "Last received seqno: sent %" PRId64 ", recv %" PRId64,
                  send_state->received, received);
    ck_assert_msg(send_state->cached     == cached,
                  "Last cached seqno: sent %" PRId64 ", recv %" PRId64,
                  send_state->cached, cached);
    ck_assert(send_state->prim_seqno    == prim_seqno);
    ck_assert(send_state->current_state == current_state);
    ck_assert(send_state->prim_state    == prim_state);
    ck_assert(send_state->prim_joined   == prim_joined);
    ck_assert(!gu_uuid_compare (&send_state->state_uuid, &state_uuid));
    ck_assert(!gu_uuid_compare (&send_state->group_uuid, &group_uuid));
    ck_assert(!gu_uuid_compare (&send_state->prim_uuid,  &prim_uuid));
    ck_assert(!strcmp(send_state->name,     name));
    ck_assert(!strcmp(send_state->inc_addr, inc_addr));

    {
        size_t str_len = 1024;
        char   send_str[str_len];
        ck_assert(gcs_state_msg_snprintf(send_str, str_len, send_state) > 0);
    }

    //v1-2
    ck_assert(send_state->appl_proto_ver == appl_proto_ver);
    //v3
    ck_assert(send_state->cached         == cached);
    //v4
    ck_assert(send_state->desync_count   == desync_count);
    //v5
    ck_assert(send_state->last_applied   == last_applied);
    ck_assert(send_state->vote_seqno     == vote_seqno);
    ck_assert(send_state->vote_res       == vote_res);
    ck_assert(send_state->vote_policy    == vote_policy);

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
    ck_assert_msg(recv_state->prim_gcs_ver  == 0, "prim_gcs_ver: %d",
                  recv_state->prim_appl_ver);
    ck_assert(send_state->prim_repl_ver == recv_state->prim_repl_ver);
    ck_assert_msg(recv_state->prim_repl_ver == 1, "prim_repl_ver: %d",
                  recv_state->prim_appl_ver);
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
        char   str[str_len];

        ck_assert(gcs_state_msg_snprintf(str, str_len, send_state) > 0);
        ck_assert(gcs_state_msg_snprintf(str, str_len, recv_state) > 0);
    }

    //v1-2
    ck_assert(send_state->appl_proto_ver == recv_state->appl_proto_ver);
    //v3
    ck_assert(send_state->cached         == recv_state->cached);
    //v4
    ck_assert(send_state->desync_count   == recv_state->desync_count);
    //v5
    ck_assert(send_state->last_applied   == recv_state->last_applied);
    ck_assert(send_state->vote_seqno     == recv_state->vote_seqno);
    ck_assert(send_state->vote_res       == recv_state->vote_res);
    ck_assert(send_state->vote_policy    == recv_state->vote_policy);

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
                                  prim2_seqno - 1, act2_seqno - 1, act2_seqno -1,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node0", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 1,
                                  act1_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 3,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, act2_seqno - 2,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
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
                                  prim1_seqno, act1_seqno, act1_seqno - 3,
                                  act1_seqno - 2, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 3,
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
                                  prim2_seqno - 1, act2_seqno - 1, -1,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
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
                                  prim1_seqno, act1_seqno, act1_seqno -3,
                                  act1_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 3,
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
                                  prim2_seqno, act2_seqno, act2_seqno - 2,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
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
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node0", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group1_uuid, &prim1_uuid,
                                  prim1_seqno, act1_seqno, act1_seqno - 3,
                                  act1_seqno - 2, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 3,
                                  GCS_NODE_STATE_JOINER,GCS_NODE_STATE_NON_PRIM,
                                  "node1", "",
                                  0, 1, 0, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group2_uuid, &prim2_uuid,
                                  prim2_seqno, act2_seqno, -1,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
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
                                  prim2_seqno - 1, act2_seqno - 1, -1,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
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
                                  prim2_seqno, act2_seqno, act2_seqno - 3,
                                  act2_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 5,
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
                                  prim1_seqno, act1_seqno, act1_seqno,
                                  act1_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 3,
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
                                  prim1_seqno, act1_seqno, act1_seqno - 2,
                                  act1_seqno - 1, GCS_SEQNO_ILL, 0,
                                  GCS_VOTE_ZERO_WINS, 3,
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

void gcs_state_msg_test_gh24(int const gcs_proto_ver)
{
    gcs_state_msg_t* st[7] = { NULL, };
    gu_uuid_t state_uuid, group_uuid;
    gu_uuid_generate(&state_uuid, NULL, 0);
    gu_uuid_generate(&group_uuid, NULL, 0);
    gu_uuid_t prim_uuid1, prim_uuid2;
    gu_uuid_generate(&prim_uuid1, NULL, 0);
    gu_uuid_generate(&prim_uuid2, NULL, 0);

    gcs_seqno_t const prim_seqno1 = 37;
    int const prim_joined1 = 3;
    uint8_t const vp1(0);
    gcs_seqno_t const prim_seqno2 = 35;
    int const prim_joined2 = 6;
    uint8_t const vp2(2);
    gcs_seqno_t const received = prim_seqno2;
    gcs_seqno_t const cached = 0;

    gcs_state_quorum_t quorum;
    // first three are 35.
    st[0] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached,
                                 received - 7, GCS_SEQNO_ILL, 0, vp2,
                                 prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home0", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[0] != 0);
    ck_assert(gcs_state_msg_vote_policy(st[0]) == vp2);
    st[1] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached,
                                 received - 11, GCS_SEQNO_ILL, 0, vp2,
                                 prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home1", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[1] != 0);
    st[2] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid2,
                                 prim_seqno2, received, cached,
                                 received - 5, GCS_SEQNO_ILL, 0, vp2,
                                 prim_joined2,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home2", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[2] != 0);

    // last four are 37.
    st[3] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached,
                                 received - 8, GCS_SEQNO_ILL, 0, vp1,
                                 prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home3", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 3);
    ck_assert(st[3] != 0);
    ck_assert(gcs_state_msg_vote_policy(st[3]) == vp1);
    st[4] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached,
                                 received - 3, GCS_SEQNO_ILL, 0, vp1,
                                 prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home4", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[4] != 0);
    st[5] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached,
                                 received - 10, GCS_SEQNO_ILL, 0, vp1,
                                 prim_joined1,
                                 GCS_NODE_STATE_SYNCED,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home5", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[5] != 0);
    st[6] = gcs_state_msg_create(&state_uuid, &group_uuid, &prim_uuid1,
                                 prim_seqno1, received, cached,
                                 received - 13, GCS_SEQNO_ILL, 0, vp1,
                                 prim_joined1,
                                 GCS_NODE_STATE_PRIM,
                                 GCS_NODE_STATE_NON_PRIM,
                                 "home6", "",
                                 gcs_proto_ver, 4, 2, 0, 0, 0,
                                 0, 2);
    ck_assert(st[6] != 0);
    int ret = gcs_state_msg_get_quorum((const gcs_state_msg_t**)st, 7,
                                       &quorum);
    ck_assert(ret == 0);
    ck_assert(quorum.primary == true);
    ck_assert(quorum.conf_id == prim_seqno1);
    switch (gcs_proto_ver)
    {
    case 0:
        ck_assert_msg(quorum.vote_policy == GCS_VOTE_ZERO_WINS,
                      "found policy %d, expected %d", quorum.vote_policy,
                      GCS_VOTE_ZERO_WINS);
        break;
    case 1:
        ck_assert_msg(quorum.vote_policy == vp1,
                      "found policy %d, expected %d", quorum.vote_policy, vp1);
        break;
    default:
        ck_abort_msg("unsupported GCS protocol: %d", gcs_proto_ver);
    }

    for(int i=0;i<7;i++) gcs_state_msg_destroy(st[i]);
}

START_TEST(gcs_state_msg_test_gh24_0) // also tests vote policy switch
{
    gcs_state_msg_test_gh24(0);
}
END_TEST

START_TEST(gcs_state_msg_test_gh24_1) // also tests vote policy switch
{
    gcs_state_msg_test_gh24(1);
}
END_TEST

/* This test is to test that protocol downgrade is disabled with state
 * excahnge >= v6 */
static void
gcs_state_msg_test_v6_upgrade(int const from_ver)
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
                                  0, 0, 0, 0,
                                  3,
                                  GCS_NODE_STATE_PRIM, GCS_NODE_STATE_PRIM,
                                  "node0", "",
                                  4, 4, 4, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[0]);

    st[1] = gcs_state_msg_create (&state_uuid, &group_uuid, &prim_uuid,
                                  prim_seqno, act_seqno, act_seqno - 3,
                                  0, 0, 0, 0,
                                  3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_JOINED,
                                  "node1", "",
                                  3, 3, 3, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[1]);

    st[2] = gcs_state_msg_create (&state_uuid, &group_uuid, &prim_uuid,
                                  prim_seqno, act_seqno, act_seqno - 3,
                                  0, 0, 0, 0,
                                  3,
                                  GCS_NODE_STATE_JOINED, GCS_NODE_STATE_JOINED,
                                  "node2", "",
                                  0, 1, 1, 0, 0, 0,
                                  0, 0);
    ck_assert(NULL != st[2]);
    st[2]->version = from_ver;

    gu_info ("                  proto_ver I");
    int
    ret = gcs_state_msg_get_quorum ((const gcs_state_msg_t**)st,
                                    sizeof(st)/sizeof(gcs_state_msg_t*),
                                    &quorum);
    ck_assert(0 == ret);
    ck_assert(from_ver == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(1 == quorum.appl_proto_ver);
    ck_assert(GCS_VOTE_ZERO_WINS == quorum.vote_policy);

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
    ck_assert(0 == quorum.vote_policy);

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
    ck_assert(from_ver == quorum.version);
    ck_assert(true == quorum.primary);
    ck_assert(0 == gu_uuid_compare(&quorum.group_uuid, &group_uuid));
    ck_assert(act_seqno  == quorum.act_id);
    ck_assert(prim_seqno == quorum.conf_id);
    ck_assert(0 == quorum.gcs_proto_ver);
    ck_assert(1 == quorum.repl_proto_ver);
    ck_assert(1 == quorum.appl_proto_ver);
    ck_assert(GCS_VOTE_ZERO_WINS == quorum.vote_policy);


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
    ck_assert(0 == quorum.vote_policy);

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
    ck_assert(0 == quorum.vote_policy);

    gcs_state_msg_destroy (st[0]);
    gcs_state_msg_destroy (st[1]);
    gcs_state_msg_destroy (st[2]);
#undef UPDATE_STATE_MSG
}

START_TEST (gcs_state_msg_test_v4v6_upgrade)
{
    gcs_state_msg_test_v6_upgrade(4);
}
END_TEST

START_TEST (gcs_state_msg_test_v5v6_upgrade)
{
    gcs_state_msg_test_v6_upgrade(5);
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
  tcase_add_test  (tc_remerge, gcs_state_msg_test_gh24_0);
  tcase_add_test  (tc_remerge, gcs_state_msg_test_gh24_1);

  suite_add_tcase (s, tc_proto_ver);
  tcase_add_test  (tc_proto_ver, gcs_state_msg_test_v4v6_upgrade);
  tcase_add_test  (tc_proto_ver, gcs_state_msg_test_v5v6_upgrade);

  return s;
}
