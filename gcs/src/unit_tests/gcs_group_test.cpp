/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../gcs_group.hpp"
#include "../gcs_act_proto.hpp"
#include "../gcs_comp_msg.hpp"

#include <check.h>
#include "gcs_group_test.hpp"
#include "gcs_test_utils.hpp"

#include "gu_inttypes.hpp"

#define TRUE (0 == 0)
#define FALSE (!TRUE)

/*
 * header will be written to buf from frg, act_len of payload will be copied
 * from act, msg structure will be filled in
 */
static void
msg_write (gcs_recv_msg_t* msg,
           gcs_act_frag_t* frg,
           char*           buf,  size_t         buf_len,
           const char*     data, size_t         data_len,
           int       sender_idx, gcs_msg_type_t type)
{
    long ret;
    ret = gcs_act_proto_write (frg, buf, buf_len);
    ck_assert_msg(0 == ret, "error code: %ld", ret);
    ck_assert(frg->frag != NULL);
    ck_assert_msg(frg->frag_len >= data_len,
                  "Resulting frag_len %lu is less than required act_len %lu\n"
                  "Refactor the test and increase buf_len.",
                  frg->frag_len, data_len);
    memcpy ((void*)frg->frag, data, data_len);

    msg->buf        = buf;
    msg->buf_len    = buf_len;
    msg->size       = (buf_len - frg->frag_len + data_len);
    msg->sender_idx = sender_idx;
    msg->type       = type;
}

static long
new_component (gcs_group_t* group, const gcs_comp_msg_t* comp)
{
    long ret = gcs_group_handle_comp_msg (group, comp);
    // modelling real state exchange is really tedious here, just fake it
    group->state = GCS_GROUP_PRIMARY;
    return ret;
}

#define LOCALHOST   "localhost"
#define REMOTEHOST  "remotehost"
#define DISTANTHOST "distanthost"

// This tests tests configuration changes
START_TEST (gcs_group_configuration)
{
    ssize_t     ret;
    gcs_group_t group;
    gcs_seqno_t seqno = 11;

    // The Action
    const char   act_buf[]   = "Test action smuction";
    ssize_t      act_len     = sizeof (act_buf);

    // lengths of three fragments of the action
    long         frag1_len    = act_len / 3;
    long         frag2_len    = frag1_len;
    long         frag3_len    = act_len - frag1_len - frag2_len;

    // pointer to the three fragments of the action
    const char*  frag1         = act_buf;
    const char*  frag2         = frag1 + frag1_len;
    const char*  frag3         = frag2 + frag2_len;

    // message buffers
    const long buf_len      = 64;
    char       buf1[buf_len], buf2[buf_len], buf3[buf_len],
               buf4[buf_len], buf5[buf_len];

    // recv message structures
    gcs_recv_msg_t msg1, msg2, msg3, msg4, msg5;
    gcs_act_frag_t frg1, frg2, frg3, frg4, frg5, frg;

    struct gcs_act_rcvd r_act;
    struct gcs_act* act = &r_act.act;

    gcs_comp_msg_t* comp;

    mark_point();

#ifndef NDEBUG
    // debug build breaks the test due to asserts
    return;
#endif

    // Initialize message parameters
    frg1.act_id    = getpid();
    frg1.act_size  = act_len;
    frg1.frag      = NULL;
    frg1.frag_len  = 0;
    frg1.frag_no   = 0;
    frg1.act_type  = GCS_ACT_WRITESET;
    frg1.proto_ver = 0;

    // normal fragments
    frg2 = frg3 = frg1;
    frg2.frag_no = frg1.frag_no + 1;
    frg3.frag_no = frg2.frag_no + 1;

    // bad fragmets to be tried instead of frg2
    frg4 = frg5 = frg2;
    frg4.act_id   = frg2.act_id + 1; // wrong action id
    frg5.act_type = GCS_ACT_SERVICE; // wrong action type

    mark_point();

    msg_write (&msg1, &frg1, buf1, buf_len, frag1, frag1_len, 0,GCS_MSG_ACTION);
    msg_write (&msg2, &frg2, buf2, buf_len, frag2, frag2_len, 0,GCS_MSG_ACTION);
    msg_write (&msg3, &frg3, buf3, buf_len, frag3, frag3_len, 0,GCS_MSG_ACTION);
    msg_write (&msg4, &frg4, buf4, buf_len, "4444",  4, 0, GCS_MSG_ACTION);
    msg_write (&msg5, &frg5, buf5, buf_len, "55555", 5, 0, GCS_MSG_ACTION);

    mark_point();

    // ready
    gu::Config cnf;
    gcs_group_register(&cnf);
    gcs_group_init (&group, &cnf, NULL, "my node", "my addr", 0, 0, 0);
    ck_assert(!gcs_group_is_primary(&group));
    ck_assert(group.num == 0);

    // Prepare first  primary component message containing only one node
    comp = gcs_comp_msg_new (TRUE, false, 0, 1, 0);
    ck_assert(comp != NULL);
    ck_assert(!gcs_comp_msg_add (comp, LOCALHOST, 0));

    ret = new_component (&group, comp);
    ck_assert(ret >= 0);
    ck_assert(gcs_group_is_primary(&group));
    ck_assert(0 == group.act_id_);
    group.act_id_ = seqno - 1;
    ck_assert(GCS_NODE_STATE_JOINED == group.nodes[0].status);

#define TRY_MESSAGE(msg)                                                \
    ret = gcs_act_proto_read (&frg, (msg).buf, (msg).size);             \
    if (0 == ret)                                                       \
        ret = gcs_group_handle_act_msg (&group, &frg, &(msg), &r_act, true);

    // 1. Try fragment that is not the first
    r_act = gcs_act_rcvd();
    //    ret = gcs_group_handle_act_msg (&group, &frg, &msg3, &r_act);
    TRY_MESSAGE(msg3);
    ck_assert_msg(ret == -EPROTO, "expected ret = %d, got %zd", -EPROTO, ret);
    ck_assert(act->buf == NULL);
    ck_assert(act->buf_len == 0);
    mark_point();

    // 2. Try first fragment
    //    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf == NULL);
    ck_assert(act->buf_len == 0);

#define TRY_WRONG_2ND_FRAGMENT(frag, res)                          \
    /*ret = gcs_group_handle_act_msg (&group, &frag, &r_act);*/    \
        TRY_MESSAGE(frag);                                         \
        ck_assert_msg(ret == res,                                  \
                      "expected ret = %d, got %zd", res, ret);     \
        ck_assert(act->buf_len == 0);

    // 3. Try first fragment again
    gu_debug ("\n\nTRY_WRONG_2ND_FRAGMENT(msg1)");
    TRY_WRONG_2ND_FRAGMENT(msg1, 0); // tolerate duplicate fragments

    // 4. Try third fragment
    gu_debug ("\n\nTRY_WRONG_2ND_FRAGMENT(msg3)");
    TRY_WRONG_2ND_FRAGMENT(msg3, -EPROTO);

    // 5. Try fourth fragment
    gu_debug ("\n\nTRY_WRONG_2ND_FRAGMENT(msg4)");
    TRY_WRONG_2ND_FRAGMENT(msg4, -EPROTO);

    // 6. Try fifth fragment
    gu_debug ("\n\nTRY_WRONG_2ND_FRAGMENT(msg5)");
    TRY_WRONG_2ND_FRAGMENT(msg5, -EPROTO);

    // 7. Try correct second fragment
    TRY_MESSAGE(msg2);
    ck_assert(ret == 0);
    ck_assert(act->buf == NULL);
    ck_assert(act->buf_len == 0);

    // 8. Try third fragment, last one
    TRY_MESSAGE(msg3);
    ck_assert(ret == act_len);
    ck_assert(r_act.sender_idx == 0);
    ck_assert(act->buf != NULL);
    ck_assert(act->buf_len == act_len);
    ck_assert_msg(r_act.id == seqno,
                  "Expected seqno %" PRId64 ", found %" PRId64,
                  seqno, r_act.id);
    seqno++;
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // 10. New component message
    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (TRUE, false, 1, 2, 0);
    ck_assert(comp != NULL);
    ck_assert(gcs_comp_msg_add (comp, REMOTEHOST, 1) >= 0);
    ck_assert(gcs_comp_msg_add (comp, LOCALHOST,  0) >= 0);

    ret = new_component (&group, comp);
    ck_assert(ret >= 0);
    ck_assert(gcs_group_is_primary(&group));
    ck_assert(GCS_NODE_STATE_JOINED == group.nodes[1].status);
    ck_assert(GCS_NODE_STATE_JOINED >  group.nodes[0].status);
    group.nodes[0].status = GCS_NODE_STATE_JOINED;

    // 11. Try the same with foreign action (now my index is 1 and sender is 0)
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    TRY_MESSAGE(msg2);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    gu_debug("\n\nTRY_MESSAGE(msg3)");
    TRY_MESSAGE(msg3);
    ck_assert_msg(ret == act_len, "Expected ret = %zd, got %zd", act_len, ret);
    ck_assert(act->buf_len == act_len);
    ck_assert(act->buf != NULL);
    ck_assert_msg(!strncmp(static_cast<const char*>(act->buf),
                           act_buf, act_len),
                  "Action received: '%s', expected '%s'",
                  static_cast<const char*>(act->buf),
                  act_buf);
    ck_assert(r_act.sender_idx == 0);
    ck_assert(act->type == GCS_ACT_WRITESET);
    ck_assert_msg(r_act.id == seqno,
                  "Expected seqno %" PRId64 ", found %" PRId64,
                  seqno, r_act.id);
    seqno++;
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // 12. Try foreign action with a new node joined in the middle.
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (TRUE, false, 1, 3, 0);
    ck_assert(comp != NULL);
    ck_assert(gcs_comp_msg_add (comp, REMOTEHOST, 1) >= 0);
    ck_assert(gcs_comp_msg_add (comp, LOCALHOST,  0) >= 0);
    ck_assert(gcs_comp_msg_add (comp, DISTANTHOST,2) >= 0);
    ret = new_component (&group, comp);
    ck_assert(ret >= 0);
    ck_assert(gcs_group_is_primary(&group));

    // now I must be able to resend the action from scratch
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    TRY_MESSAGE(msg2);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    TRY_MESSAGE(msg3);
    ck_assert(ret == act_len);
    ck_assert(act->buf_len == act_len);
    ck_assert(act->buf != NULL);
    ck_assert_msg(!strncmp(static_cast<const char*>(act->buf),
                           act_buf, act_len),
                  "Action received: '%s', expected '%s'",
                  static_cast<const char*>(act->buf),
                  act_buf);
    ck_assert(r_act.sender_idx == 0);
    ck_assert(act->type == GCS_ACT_WRITESET);
    ck_assert_msg(r_act.id == seqno,
                  "Expected seqno %" PRId64 ", found %" PRId64,
                  seqno, r_act.id);
    seqno++;
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // 13. Try to send an action with one node disappearing in the middle
    //     and order of nodes changed

    // 13.1 Each node sends a message
    msg_write (&msg1, &frg1, buf1, buf_len, frag1, frag1_len, 0,GCS_MSG_ACTION);
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    msg_write (&msg1, &frg1, buf1, buf_len, frag1, frag1_len, 1,GCS_MSG_ACTION);
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    msg_write (&msg1, &frg1, buf1, buf_len, frag1, frag1_len, 2,GCS_MSG_ACTION);
    TRY_MESSAGE(msg1);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    // 13.2 configuration changes, one node disappears
    // (REMOTEHOST, LOCALHOST, DISTANTHOST) -> (LOCALHOST, REMOTEHOST)
    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (TRUE, false, 0, 2, 0);
    ck_assert(comp != NULL);
    ck_assert(gcs_comp_msg_add (comp, LOCALHOST, 0) >= 0);
    ck_assert(gcs_comp_msg_add (comp, REMOTEHOST,1) >= 0);
    ret = new_component (&group, comp);
    ck_assert(ret >= 0);
    ck_assert(gcs_group_is_primary(&group));
    ck_assert(group.act_id_ + 1 == seqno);
    ck_assert(GCS_NODE_STATE_JOINED == group.nodes[1].status);
    ck_assert(GCS_NODE_STATE_JOINED == group.nodes[0].status);

    // 13.3 now I just continue sending messages
    TRY_MESSAGE(msg2);
    ck_assert_msg(ret == 0, "%zd (%s)", ret, strerror(-ret));
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    msg_write (&msg2, &frg2, buf2, buf_len, frag2, frag2_len, 1,GCS_MSG_ACTION);
    TRY_MESSAGE(msg2);
    ck_assert(ret == 0);
    ck_assert(act->buf_len == 0);
    ck_assert(act->buf == NULL);

    // local message - action must be resent
    gu_debug("\n\nLocal message 3");
    TRY_MESSAGE(msg3);
    ck_assert(ret == act_len);
    ck_assert(act->buf_len == act_len);
    ck_assert(act->buf != NULL);
    ck_assert(r_act.sender_idx == 0);
    ck_assert(act->type == GCS_ACT_WRITESET);
    ck_assert_msg(!strncmp((const char*)act->buf, act_buf, act_len),
                  "Action received: '%s', expected '%s'",
                  static_cast<const char*>(act->buf),
                  act_buf);
    ck_assert_msg(r_act.id == -ERESTART, "Expected seqno %d, found %" PRId64,
                  -ERESTART, r_act.id);
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // foreign message - action must be dropped (ignored)
    gu_debug("\n\nForeign message 3");
    msg_write (&msg3, &frg3, buf3, buf_len, frag3, frag3_len, 1,GCS_MSG_ACTION);
    TRY_MESSAGE(msg3);
    ck_assert_msg(ret == 0,
                  "Expected ret 0, got %zd", ret);
    ck_assert_msg(act->buf_len == 0,
                  "Expected buf_len 0, got %zd", act->buf_len);
    ck_assert(act->buf == NULL);
    ck_assert_msg(r_act.sender_idx == -1,
                  "Expected action sender -1, got %d", r_act.sender_idx);
    ck_assert(act->type == GCS_ACT_ERROR);
    ck_assert_msg(r_act.id == GCS_SEQNO_ILL,
                  "Expected seqno %" PRId64 ", found %" PRId64,
                  GCS_SEQNO_ILL, r_act.id);
    ck_assert(group.act_id_ + 1 == seqno);
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // Leave group
    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (FALSE, false, -1, 0, 0);
    ck_assert(comp != NULL);

    ret = new_component (&group, comp);
    ck_assert(ret >= 0);
    gcs_comp_msg_delete (comp);
    gcs_group_free(&group);
}
END_TEST

// This tests last applied functionality
static void
test_last_applied(int const gcs_proto_ver)
{
    // Create 4-node component
    gt_group gt(4, gcs_proto_ver, true);
    // group object of the 0th node
    gcs_group_t& group(*gt.nodes[0]->group());

    // 0, 0, 0, 0
    ck_assert(group.last_applied == 0);
    gt.deliver_last_applied (0, 11);
    // 11, 0, 0, 0
    ck_assert_msg(group.last_applied == 0,
                  "expected last_applied = 0, got %" PRId64,
                  group.last_applied);
    gt.deliver_last_applied (1, 12);
    // 11, 12, 0, 0
    ck_assert(group.last_applied == 0);
    gt.deliver_last_applied (2, 13);
    // 11, 12, 13, 0
    ck_assert(group.last_applied == 0);
    gt.deliver_last_applied (3, 14);
    // 11, 12, 13, 14
    assert(group.last_applied == 11);
    ck_assert(group.last_applied == 11); // now must be 11
    gt.deliver_last_applied (1, 16);
    // 11, 16, 13, 14
    ck_assert(group.last_applied == 11); // now must still be 11
    gt.deliver_last_applied (0, 17);
    // 17, 16, 13, 14
    ck_assert(group.last_applied == 13); // now must be 13
    gt.deliver_last_applied (3, 18);
    // 17, 16, 13, 18
    ck_assert(group.last_applied == 13); // must still be 13

    // remove the lagging node
    struct gt_node* const gn(gt.drop_node(2));
    ck_assert(gn != NULL);
    delete gn;

    // 17, 16, 18
    // With GCS protocol 2 and above we use conservative group wide value from
    // the previous PC (13) as opposed to the minimal individual value (16)
    gcs_seqno_t const expect1(gcs_proto_ver < 2 ? 16 : 13);
    ck_assert_msg(group.last_applied == expect1,
                  "Expected %" PRId64 ", got %" PRId64 "\n"
                  "Nodes: %ld; last_applieds: "
                  "%" PRId64 ", %" PRId64 " , %" PRId64,
                  expect1, group.last_applied, group.num,
                  group.nodes[0].last_applied, group.nodes[1].last_applied,
                  group.nodes[2].last_applied);

    if (gcs_proto_ver >= 2)
    {
        ck_assert(13 == group.nodes[0].last_applied);
        ck_assert(13 == group.nodes[1].last_applied);
        ck_assert(13 == group.nodes[2].last_applied);
    }

    // add new node
    ck_assert(0 == gt.add_node(new gt_node(DISTANTHOST"1", gcs_proto_ver),true));
    ck_assert(0 == gt.sync_node(gt.nodes_num - 1));
    // 17, 16, 18, 0 (v0-1) / 13, 13, 13, 13 (v2-)
    // With GCS protocol 2 and above last_applied can't go down.
    gcs_seqno_t const expect2(gcs_proto_ver < 2 ? 0 : 13);
    ck_assert(group.last_applied == expect2);
}

START_TEST(gcs_group_last_applied_v0)
{
    test_last_applied(0);
}
END_TEST

START_TEST(gcs_group_last_applied_v1)
{
    test_last_applied(1);
}
END_TEST

START_TEST(gcs_group_last_applied_v2)
{
    test_last_applied(2);
}
END_TEST

START_TEST(test_gcs_group_find_donor)
{
    gu::Config cnf;
    gcs_group_register(&cnf);
    gcs_group_t group;
    gcs_group_init(&group, &cnf, NULL, "", "", 0, 0, 0);
    const char* s_group_uuid = "0d0d0d0d-0d0d-0d0d-0d0d-0d0d0d0d0d0d";
    gu_uuid_scan(s_group_uuid, strlen(s_group_uuid), &group.group_uuid);

    // five nodes
    // idx name segment  seqno
    // 0th home0 0        90
    // 1th home1 0        95
    // 2th home2 0        105
    // 3th home3 0(joiner)100
    // 4th home4 1        90
    // 5th home5 1        95
    // 6th home6 1        105

    const int number = 7;
    group_nodes_free(&group);
    group.nodes = (gcs_node_t*)malloc(sizeof(gcs_node_t) * number);
    group.num = number;
    const gcs_seqno_t seqnos[] = {90, 95, 105, 100, 90, 95, 105};
    gcs_node_t* nodes = group.nodes;
    const int joiner = 3;

    for(int i = 0; i < number; i++)
    {
        uint8_t const vp(gcs_group_conf_to_vote_policy(cnf));
        char name[32];
        snprintf(name, sizeof(name), "home%d", i);
        gcs_node_init(&nodes[i], NULL, name, name,
                      "", 0, 0, 0, i > joiner ? 1 : 0);
        nodes[i].status = GCS_NODE_STATE_SYNCED;
        nodes[i].state_msg = gcs_state_msg_create(
            &GU_UUID_NIL, &GU_UUID_NIL, &GU_UUID_NIL,
            0, 0, seqnos[i], 0,
            GCS_SEQNO_ILL, 0, vp, 0,
            GCS_NODE_STATE_SYNCED, GCS_NODE_STATE_SYNCED,
            "", "",
            0, 0, 0, 0, 0, 0,
            0, 0);
    }

    group.quorum.act_id = 0; // in safe range.
    ck_assert(group.quorum.gcs_proto_ver == -1);
    ck_assert(group.gcs_proto_ver == 0);

    int donor = -1;

    const int sv = 2; // str version.
#define SARGS(s) s, strlen(s)
    //========== sst ==========
    gu::GTID const empty_gtid;
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home3"),
                                 empty_gtid);
    ck_assert(donor == -EHOSTDOWN);

    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home1,home2"),
                                 empty_gtid);
    ck_assert(donor == 1);

    nodes[1].status = GCS_NODE_STATE_JOINER;
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home1,home2"),
                                 empty_gtid);
    ck_assert(donor == 2);
    nodes[1].status = GCS_NODE_STATE_SYNCED;

    // handle dangling comma.
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home3,"),
                                 empty_gtid);
    ck_assert(donor == 0);

    // ========== ist ==========
    // by name.
    gu::GTID const group_gtid(group.group_uuid, 100);
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home0,home1,home2"),
                                 group_gtid);
    ck_assert(donor == 1);

    group.quorum.act_id = 1498; // not in safe range.
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home2"),
                                 group_gtid);
    ck_assert(donor == 2);

    group.quorum.act_id = 1497; // in safe range. in segment.
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home2"),
                                 group_gtid);
    ck_assert(donor == 1);

    group.quorum.act_id = 1497; // in safe range. cross segment.
    nodes[0].status = GCS_NODE_STATE_JOINER;
    nodes[1].status = GCS_NODE_STATE_JOINER;
    nodes[2].status = GCS_NODE_STATE_JOINER;
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home2"),
                                 group_gtid);
    ck_assert(donor == 5);
    nodes[0].status = GCS_NODE_STATE_SYNCED;
    nodes[1].status = GCS_NODE_STATE_SYNCED;
    nodes[2].status = GCS_NODE_STATE_SYNCED;
#undef SARGS

    gcs_group_free(&group);
}
END_TEST

Suite *gcs_group_suite(void)
{
    Suite *suite = suite_create("GCS group context");
    TCase *tcase = tcase_create("gcs_group");

    suite_add_tcase (suite, tcase);

    tcase_add_test  (tcase, gcs_group_configuration);
    tcase_add_test  (tcase, gcs_group_last_applied_v0);
    tcase_add_test  (tcase, gcs_group_last_applied_v1);
    tcase_add_test  (tcase, gcs_group_last_applied_v2);
    tcase_add_test  (tcase, test_gcs_group_find_donor);

    return suite;
}
