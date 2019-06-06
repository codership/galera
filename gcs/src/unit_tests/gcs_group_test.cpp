/*
 * Copyright (C) 2008-2019 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../gcs_group.hpp"
#include "../gcs_act_proto.hpp"
#include "../gcs_comp_msg.hpp"

#include "gcs_group_test.hpp" // must be included last

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
           long       sender_idx, gcs_msg_type_t type)
{
    long ret;
    ret = gcs_act_proto_write (frg, buf, buf_len);
    fail_if (ret, "error code: %d", ret);
    fail_if (frg->frag == NULL);
    fail_if (frg->frag_len < data_len,
             "Resulting frag_len %lu is less than required act_len %lu\n"
             "Refactor the test and increase buf_len.", frg->frag_len,data_len);
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
//    group->state = GCS_GROUP_PRIMARY;
    return ret;
}

// just pretend we received SYNC message
//#define RECEIVE_SYNC() group.new_memb = FALSE;
#define RECEIVE_SYNC()
#define LOCALHOST   "localhost"
#define REMOTEHOST  "remotehost"
#define DISTANTHOST "distanthost"

// This tests tests configuration changes
START_TEST (gcs_group_configuration)
{
    ssize_t     ret;
    gcs_group_t group;
    gcs_seqno_t seqno = 1;

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
    char         buf1[buf_len], buf2[buf_len], buf3[buf_len],
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
    fail_if (gcs_group_is_primary(&group));
    fail_if (group.num != 1);

    // Prepare first  primary component message containing only one node
    comp = gcs_comp_msg_new (TRUE, false, 0, 1, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST, 0));

    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (!gcs_group_new_members(&group)); RECEIVE_SYNC();

#define TRY_MESSAGE(msg) \
    ret = gcs_act_proto_read (&frg, (msg).buf, (msg).size);     \
    ret = gcs_group_handle_act_msg (&group, &frg, &(msg), &r_act, true);

    // 1. Try fragment that is not the first
    r_act = gcs_act_rcvd();
    //    ret = gcs_group_handle_act_msg (&group, &frg, &msg3, &r_act);
    TRY_MESSAGE(msg3);
    fail_if (ret != -EPROTO);
    fail_if (act->buf != NULL);
    fail_if (act->buf_len != 0);
    mark_point();

    // 2. Try first fragment
//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf != NULL);
    fail_if (act->buf_len != 0);

#define TRY_WRONG_2ND_FRAGMENT(frag)                      \
    /*ret = gcs_group_handle_act_msg (&group, &frag, &r_act);*/    \
    TRY_MESSAGE(frag);                                    \
    fail_if (ret != -EPROTO);                             \
    fail_if (act->buf_len != 0);

    // 3. Try first fragment again
    gu_debug ("");
    TRY_WRONG_2ND_FRAGMENT(msg1);
    gu_debug ("");

    // 4. Try third fragment
    TRY_WRONG_2ND_FRAGMENT(msg3);

    // 5. Try fouth fragment
    TRY_WRONG_2ND_FRAGMENT(msg4);

    // 6. Try fifth fragment
    TRY_WRONG_2ND_FRAGMENT(msg5);

    // 7. Try correct second fragment
//    ret = gcs_group_handle_act_msg (&group, &msg2, &r_act);
    TRY_MESSAGE(msg2);
    fail_if (ret != 0);
    fail_if (act->buf != NULL); act->buf = (void*)0x12354; // shall be NULLed
    fail_if (act->buf_len != 0);

    // 8. Try third fragment, last one
//    ret = gcs_group_handle_act_msg (&group, &msg3, &r_act);
    TRY_MESSAGE(msg3);
    fail_if (ret != act_len);
    fail_if (r_act.sender_idx != 0);
    fail_if (act->buf != NULL); // local action, must be fetched from local fifo
    fail_if (act->buf_len != act_len);
    fail_if (r_act.id != seqno, "Expected seqno %llu, found %llu", seqno, r_act.id);
    seqno++;
    // cleanup
    r_act = gcs_act_rcvd();

    // 10. New component message
    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (TRUE, false, 1, 2, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, REMOTEHOST, 1) < 0);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST,  0) < 0);

    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (!gcs_group_new_members(&group)); RECEIVE_SYNC();

    // 11. Try the same with foreign action (now my index is 1, sender is 0)
//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);
//    ret = gcs_group_handle_act_msg (&group, &msg2, &r_act);
    TRY_MESSAGE(msg2);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);
//    ret = gcs_group_handle_act_msg (&group, &msg3, &r_act);
    TRY_MESSAGE(msg3);
    fail_if (ret != act_len, "Expected ret = %zd, got %zd", act_len, ret);
    fail_if (act->buf_len != act_len);
    fail_if (act->buf == NULL);
    fail_if (strncmp((const char*)act->buf, act_buf, act_len),
             "Action received: '%s', expected '%s'", act_buf);
    fail_if (r_act.sender_idx != 0);
    fail_if (act->type != GCS_ACT_WRITESET);
    fail_if (r_act.id != seqno, "Expected seqno %llu, found %llu", seqno, r_act.id);
    seqno++;
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // 12. Try foreign action with a new node joined in the middle.
    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (TRUE, false, 1, 3, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, REMOTEHOST, 1) < 0);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST,  0) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST,2) < 0);

//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);

    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (!gcs_group_new_members(&group)); RECEIVE_SYNC();

    // now I must be able to resend the action from scratch
//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);
//    ret = gcs_group_handle_act_msg (&group, &msg2, &r_act);
    TRY_MESSAGE(msg2);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);
//    ret = gcs_group_handle_act_msg (&group, &msg3, &r_act);
    TRY_MESSAGE(msg3);
    fail_if (ret != act_len);
    fail_if (act->buf_len != act_len);
    fail_if (act->buf == NULL);
    fail_if (strncmp((const char*)act->buf, act_buf, act_len),
             "Action received: '%s', expected '%s'", act_buf);
    fail_if (r_act.sender_idx != 0);
    fail_if (act->type != GCS_ACT_WRITESET);
    fail_if (r_act.id != seqno, "Expected seqno %llu, found %llu", seqno, r_act.id);
    seqno++;
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // 13. Try to send an action with one node disappearing in the middle
    //     and order of nodes changed

    // 13.1 Each node sends a message
//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);

    msg_write (&msg1, &frg1, buf1, buf_len, frag1, frag1_len, 1,GCS_MSG_ACTION);
//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);

    msg_write (&msg1, &frg1, buf1, buf_len, frag1, frag1_len, 2,GCS_MSG_ACTION);
//    ret = gcs_group_handle_act_msg (&group, &msg1, &r_act);
    TRY_MESSAGE(msg1);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);

    // 13.2 configuration changes, one node disappears
    // (REMOTEHOST, LOCALHOST, DISTANTHOST) -> (LOCALHOST, REMOTEHOST)
    gcs_comp_msg_delete (comp);
    comp = gcs_comp_msg_new (TRUE, false, 0, 2, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST, 0) < 0);
    fail_if (gcs_comp_msg_add (comp, REMOTEHOST,1) < 0);
    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (gcs_group_new_members(&group), "Nodes: %d: node0 - '%s', "
//             "node1 - '%s'", group.num,
//             group.nodes[0].id, group.nodes[1].id);
    RECEIVE_SYNC();
    gcs_comp_msg_delete (comp);
return;
    // 13.3 now I just continue sending messages
//    ret = gcs_group_handle_act_msg (&group, &msg2, &r_act); // local
    TRY_MESSAGE(msg2);
    fail_if (ret != 0, "%d (%s)", ret, strerror(-ret));
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);
    msg_write (&msg2, &frg2, buf2, buf_len, frag2, frag2_len, 1,GCS_MSG_ACTION);
//    ret = gcs_group_handle_act_msg (&group, &msg2, &r_act); // foreign
    TRY_MESSAGE(msg2);
    fail_if (ret != 0);
    fail_if (act->buf_len != 0);
    fail_if (act->buf != NULL);
    act->buf = (void*)0x11111; // shall be NULLed below when local act is recvd
//    ret = gcs_group_handle_act_msg (&group, &msg3, &r_act); // local
    TRY_MESSAGE(msg3);
    fail_if (ret != act_len);
    fail_if (act->buf_len != act_len);
    fail_if (act->buf != NULL);
    fail_if (r_act.sender_idx != 0);
    fail_if (act->type != GCS_ACT_WRITESET);
    fail_if (r_act.id != seqno, "Expected seqno %llu, found %llu", seqno, r_act.id);
    seqno++;

    msg_write (&msg3, &frg3, buf3, buf_len, frag3, frag3_len, 1,GCS_MSG_ACTION);
//    ret = gcs_group_handle_act_msg (&group, &msg3, &r_act); // foreign
    TRY_MESSAGE(msg3);
    fail_if (ret != act_len);
    fail_if (act->buf_len != act_len);
    fail_if (act->buf == NULL);
    fail_if (strncmp((const char*)act->buf, act_buf, act_len),
             "Action received: '%s', expected '%s'", act_buf);
    fail_if (r_act.sender_idx != 1);
    fail_if (act->type != GCS_ACT_WRITESET);
    fail_if (r_act.id != seqno, "Expected seqno %llu, found %llu", seqno, r_act.id);
    seqno++;
    // cleanup
    free ((void*)act->buf);
    r_act = gcs_act_rcvd();

    // Leave group
    comp = gcs_comp_msg_new (FALSE, false, -1, 0, 0);
    fail_if (comp == NULL);

    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (gcs_group_is_primary(&group));
    // comment until implemented: fail_if (!gcs_group_new_members(&group)); RECEIVE_SYNC();
}
END_TEST

static inline void
group_set_last_msg (gcs_recv_msg_t* msg, gcs_seqno_t seqno)
{
    *(gcs_seqno_t*)(msg->buf) = gcs_seqno_htog (seqno);
}

static inline gcs_seqno_t
group_get_last_msg (gcs_recv_msg_t* msg)
{
    return gcs_seqno_gtoh(*(gcs_seqno_t*)(msg->buf));
}

// This tests last applied functionality
START_TEST(gcs_group_last_applied)
{
    long            ret;
    gcs_group_t     group;
    gcs_comp_msg_t* comp;
    gcs_recv_msg_t  msg0, msg1, msg2, msg3;
    uint8_t         buf0[sizeof(gcs_seqno_t)];
    uint8_t         buf1[sizeof(gcs_seqno_t)];
    uint8_t         buf2[sizeof(gcs_seqno_t)];
    uint8_t         buf3[sizeof(gcs_seqno_t)];

    // set up message structures
    msg0.type = GCS_MSG_LAST;
    msg0.buf_len = sizeof(gcs_seqno_t);
    msg0.size = sizeof(gcs_seqno_t);
    msg1 = msg2 = msg3 = msg0;
    msg0.buf = buf0; msg1.buf = buf1; msg2.buf = buf2; msg3.buf = buf3;
    msg0.sender_idx = 0; msg1.sender_idx = 1;
    msg2.sender_idx = 2; msg3.sender_idx = 3;

    // Create 4-node component
    comp = gcs_comp_msg_new (TRUE, false, 0, 4, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST,     0) < 0);
    fail_if (gcs_comp_msg_add (comp, REMOTEHOST,    1) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST"1",2) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST"2",2) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST"2",2) >= 0);

    gu::Config cnf;
    gcs_group_register(&cnf);
    gcs_group_init(&group, &cnf, NULL, "", "", 0, 0, 1);
    mark_point();
    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (!gcs_group_new_members(&group)); RECEIVE_SYNC();

    // 0, 0, 0, 0
    fail_if (group.last_applied != 0);
    group_set_last_msg (&msg0, 1);
    fail_if (1 != group_get_last_msg(&msg0));
    gcs_group_handle_last_msg (&group, &msg0);
    // 1, 0, 0, 0
    fail_if (group.last_applied != 0); // smallest is still 0
    group_set_last_msg (&msg1, 2);
    gcs_group_handle_last_msg (&group, &msg1);
    // 1, 2, 0, 0
    fail_if (group.last_applied != 0); // smallest is still 0
    group_set_last_msg (&msg2, 3);
    gcs_group_handle_last_msg (&group, &msg2);
    // 1, 2, 3, 0
    fail_if (group.last_applied != 0); // smallest is still 0
    group_set_last_msg (&msg3, 4);
    gcs_group_handle_last_msg (&group, &msg3);
    // 1, 2, 3, 4
    fail_if (group.last_applied != 1); // now must be 1
    group_set_last_msg (&msg1, 6);
    gcs_group_handle_last_msg (&group, &msg1);
    // 1, 6, 3, 4
    fail_if (group.last_applied != 1); // now must still be 1
    group_set_last_msg (&msg0, 7);
    gcs_group_handle_last_msg (&group, &msg0);
    // 7, 6, 3, 4
    fail_if (group.last_applied != 3); // now must be 3
    group_set_last_msg (&msg3, 8);
    gcs_group_handle_last_msg (&group, &msg3);
    // 7, 6, 3, 8
    fail_if (group.last_applied != 3); // must still be 3

    // remove the lagging node
    gcs_comp_msg_delete(comp);
    comp = gcs_comp_msg_new (TRUE, false, 0, 3, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST,     0) < 0);
    fail_if (gcs_comp_msg_add (comp, REMOTEHOST,    1) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST"2",2) < 0);

    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (gcs_group_new_members(&group)); RECEIVE_SYNC();
    // 7, 6, 8
    fail_if (group.last_applied != 6,
             "Expected %u, got %llu\nGroup: %d: %s, %s, %s",
             6, group.last_applied,
             group.num, group.nodes[0].id, group.nodes[1].id,group.nodes[2].id);

    // add new node
    gcs_comp_msg_delete(comp);
    comp = gcs_comp_msg_new (TRUE, false, 0, 4, 0);
    fail_if (comp == NULL);
    fail_if (gcs_comp_msg_add (comp, LOCALHOST,     0) < 0);
    fail_if (gcs_comp_msg_add (comp, REMOTEHOST,    1) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST"2",2) < 0);
    fail_if (gcs_comp_msg_add (comp, DISTANTHOST"1",2) < 0);

    ret = new_component (&group, comp);
    fail_if (ret < 0);
//    fail_if (!gcs_group_is_primary(&group));
//    fail_if (!gcs_group_new_members(&group));
    // 7, 6, 8, 0
    fail_if (group.last_applied != 0);

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
    fail_if (group.quorum.gcs_proto_ver != -1);
    fail_if (group.gcs_proto_ver != 0);

    int donor = -1;

    const int sv = 2; // str version.
#define SARGS(s) s, strlen(s)
    //========== sst ==========
    gu::GTID const empty_gtid;
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home3"),
                                 empty_gtid);
    fail_if(donor != -EHOSTDOWN);

    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home1,home2"),
                                 empty_gtid);
    fail_if(donor != 1);

    nodes[1].status = GCS_NODE_STATE_JOINER;
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home1,home2"),
                                 empty_gtid);
    fail_if(donor != 2);
    nodes[1].status = GCS_NODE_STATE_SYNCED;

    // handle dangling comma.
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home3,"),
                                 empty_gtid);
    fail_if(donor != 0);

    // ========== ist ==========
    // by name.
    gu::GTID const group_gtid(group.group_uuid, 100);
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home0,home1,home2"),
                                 group_gtid);
    fail_if(donor != 1);

    group.quorum.act_id = 1498; // not in safe range.
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home2"),
                                 group_gtid);
    fail_if(donor != 2);

    group.quorum.act_id = 1497; // in safe range. in segment.
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home2"),
                                 group_gtid);
    fail_if(donor != 1);

    group.quorum.act_id = 1497; // in safe range. cross segment.
    nodes[0].status = GCS_NODE_STATE_JOINER;
    nodes[1].status = GCS_NODE_STATE_JOINER;
    nodes[2].status = GCS_NODE_STATE_JOINER;
    donor = gcs_group_find_donor(&group, sv, joiner, SARGS("home2"),
                                 group_gtid);
    fail_if(donor != 5);
    nodes[0].status = GCS_NODE_STATE_SYNCED;
    nodes[1].status = GCS_NODE_STATE_SYNCED;
    nodes[2].status = GCS_NODE_STATE_SYNCED;
#undef SARGS

    // todo: free
    for(int i = 0; i < number; i++)
    {
        gcs_state_msg_destroy((gcs_state_msg_t*)nodes[i].state_msg);
    }
    free(nodes);
}
END_TEST

Suite *gcs_group_suite(void)
{
    Suite *suite = suite_create("GCS group context");
    TCase *tcase = tcase_create("gcs_group");
    TCase *tcase_ignore = tcase_create("gcs_group");

    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase_ignore, gcs_group_configuration);
    tcase_add_test  (tcase_ignore, gcs_group_last_applied);
    tcase_add_test  (tcase, test_gcs_group_find_donor);

    return suite;
}
