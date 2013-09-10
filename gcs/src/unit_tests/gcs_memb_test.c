/*
 * Copyright (C) 2011-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>

#include "gcs_memb_test.h"

#include "../gcs_group.h"
#include "../gcs_comp_msg.h"

#include <gu_uuid.h>
#include <stdbool.h>

struct node
{
    gcs_group_t group;
    const char id[GCS_COMP_MEMB_ID_MAX_LEN + 1]; /// ID assigned by the backend
//    gcs_segment_t segment;
};

#define MAX_NODES 10

struct group
{
    struct node* nodes[MAX_NODES];
    int          nodes_num;
};

/* delivers new component message to all memebers */
static long
deliver_component_msg (struct group* group, bool prim)
{
    int i;

    for (i = 0; i < group->nodes_num; i++) {
        gcs_comp_msg_t* msg = gcs_comp_msg_new (prim, false, i,
                                                group->nodes_num);
        if (msg) {
            int j;

            for (j = 0; j < group->nodes_num; j++) {
                const struct node* const node = group->nodes[j];
                long ret = gcs_comp_msg_add (msg, node->id, j);
                fail_if (j != ret, "Failed to add %d member: %ld (%s)",
                         j, ret, strerror(-ret));
            }

            /* check component message */
            fail_if (i != gcs_comp_msg_self(msg));
            fail_if (group->nodes_num != gcs_comp_msg_num(msg));
            for (j = 0; j < group->nodes_num; j++) {
                const char* const src_id = group->nodes[j]->id;
                const char* const dst_id = gcs_comp_msg_member(msg, j)->id;
                fail_if (strcmp(src_id, dst_id),
                         "%d node id %s, recorded in comp msg as %s",
                         j, src_id, dst_id);
//                gcs_segment_t const src_seg = group->nodes[j]->segment;
                gcs_segment_t const dst_seg = gcs_comp_msg_member(msg, j)->segment;
                fail_if (j != dst_seg,
                         "%d node segment %d, recorded in comp msg as %d",
                         j, j, (int)dst_seg);
            }

            gcs_group_state_t ret =
                gcs_group_handle_comp_msg (&(group->nodes[i]->group), msg);

            fail_if (ret != GCS_GROUP_WAIT_STATE_UUID);

            gcs_comp_msg_delete (msg);

            /* check that uuids are properly recorded in internal structures */
            for (j = 0; j < group->nodes_num; j++) {
                const char* src_id = group->nodes[j]->id;
                const char* dst_id = group->nodes[i]->group.nodes[j].id;
                fail_if (strcmp(src_id, dst_id),
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

#if 0
static long
group_send_msg (struct group* group, gcs_group_t* node,
                const void* msg, ssize_t msg_len)
{
    return 0;
}
#endif

static long
perform_state_exchange (struct group* group)
{
    /* first deliver state uuid message */
    gu_uuid_t state_uuid;
    gu_uuid_generate (&state_uuid, NULL, 0);

    gcs_recv_msg_t uuid_msg =
    {
        .buf        = &state_uuid,
        .buf_len    = sizeof (state_uuid),
        .size       = sizeof (state_uuid),
        .sender_idx = 0,
        .type       = GCS_MSG_STATE_UUID
    };

    gcs_group_state_t state;
    int i;
    for (i = 0; i < group->nodes_num; i++) {
        state = gcs_group_handle_uuid_msg (&(group->nodes[i]->group),&uuid_msg);
        fail_if (state != GCS_GROUP_WAIT_STATE_MSG,
                 "Wrong group state after STATE_UUID message. "
                 "Expected: %d, got: %d", GCS_GROUP_WAIT_STATE_MSG, state);
    }

    /* complete state message exchange */
    for (i = 0; i < group->nodes_num; i++)
    {
        /* create state message from node i */
        gcs_state_msg_t* state =
            gcs_group_get_state (&(group->nodes[i]->group));
        fail_if (NULL == state);

        ssize_t state_len = gcs_state_msg_len (state);
        uint8_t state_buf[state_len];
        gcs_state_msg_write (state_buf, state);

        gcs_recv_msg_t state_msg =
        {
            .buf        = state_buf,
            .buf_len    = sizeof (state_buf),
            .size       = sizeof (state_buf),
            .sender_idx = i,
            .type       = GCS_MSG_STATE_MSG
        };

        /* deliver to each of the nodes */
        int j;
        for (j = 0; j < group->nodes_num; j++) {
            gcs_group_state_t ret =
                gcs_group_handle_state_msg (&(group->nodes[j]->group),
                                            &state_msg);

            if (group->nodes_num - 1 == i) { // a message from the last node
                fail_if (ret != GCS_GROUP_PRIMARY,
                         "Handling state msg failed: sender %d, receiver %d",
                         i, j);
            }
            else {
                fail_if (ret != GCS_GROUP_WAIT_STATE_MSG,
                         "Handling state msg failed: sender %d, receiver %d",
                         i, j);
            }
        }

        gcs_state_msg_destroy (state);
    }

    return 0;
}

static long
group_add_node (struct group* group, struct node* node, bool new_id)
{
    if (new_id) {
        gu_uuid_t node_uuid;
        gu_uuid_generate (&node_uuid, NULL, 0);
        gu_uuid_print (&node_uuid, (char*)node->id, sizeof (node->id));
        gu_debug ("Node %d (%p) UUID: %s", group->nodes_num, node, node->id);
    }

    group->nodes[group->nodes_num] = node;
    group->nodes_num++;

    /* check that all node ids are different */
    int i;
    for (i = 0; i < group->nodes_num; i++) {
        int j;
        for (j = i+1; j < group->nodes_num; j++) {
            fail_if (!strcmp (group->nodes[i]->id, group->nodes[j]->id),
                     "%d (%p) and %d (%p) have the same id: %s/%s", 
                     i, group->nodes[i], j,group->nodes[j],
                     group->nodes[i]->id, group->nodes[j]->id);
        }
    }

    /* deliver new component message to all nodes */
    long ret = deliver_component_msg (group, true);
    fail_if (ret != 0, "Component message delivery failed: %d (%s)",
             ret, strerror(-ret));

    /* deliver state exchange uuid */
    ret = perform_state_exchange (group);
    fail_if (ret != 0, "State exchange failed: %d (%s)",
             ret, strerror(-ret));

    return 0;
}

/* NOTE: this function uses simplified and determinitstic algorithm where
 *       dropped node is always replaced by the last one in group.
 *       For our purposes (reproduction of #465) it fits perfectly. */
static struct node*
group_drop_node (struct group* group, int idx)
{
    struct node* dropped = group->nodes[idx];

    group->nodes[idx] = group->nodes[group->nodes_num - 1];
    group->nodes[group->nodes_num - 1] = NULL;
    group->nodes_num--;

    if (group->nodes_num > 0) {
        deliver_component_msg (group, true);
        perform_state_exchange (group);
    }

    return dropped;
}

static gcs_node_state_t
get_node_state (struct node* node)
{
    return node->group.nodes[node->group.my_idx].status;
}

/* for delivery of GCS_MSG_SYNC or GCS_MSG_JOIN msg*/
static long
deliver_join_sync_msg (struct group* const group, int const src,
                       gcs_msg_type_t type)
{
    gcs_seqno_t    seqno = group->nodes[src]->group.act_id;
    gcs_recv_msg_t msg =
    {
        .buf        = &seqno,
        .buf_len    = sizeof (seqno),
        .size       = sizeof (seqno),
        .sender_idx = src,
        .type       = type
    };

    long ret = -1;
    int i;
    for (i = 0; i < group->nodes_num; i++) {
        gcs_group_t* const gr = &group->nodes[i]->group;
        switch (type) {
        case GCS_MSG_JOIN:
            ret = gcs_group_handle_join_msg(gr, &msg);
            if (i == src) {
                fail_if (ret != 1,
                         "%d failed to handle own JOIN message: %d (%s)",
                         i, ret, strerror (-ret));
            }
            else {
                fail_if (ret != 0,
                         "%d failed to handle other JOIN message: %d (%s)",
                         i, ret, strerror (-ret));
            }
            break;
        case GCS_MSG_SYNC:
            ret = gcs_group_handle_sync_msg(gr, &msg);
            if (i == src) {
                fail_if (ret != 1 &&
                         gr->nodes[src].status == GCS_NODE_STATE_JOINED,
                         "%d failed to handle own SYNC message: %d (%s)",
                         i, ret, strerror (-ret));
            }
            else {
                fail_if (ret != 0,
                         "%d failed to handle other SYNC message: %d (%s)",
                         i, ret, strerror (-ret));
            }
            break;
        default:
            fail ("wrong message type: %d", type);
        }
    }

    return ret;
}

static bool
verify_node_state_across_group (struct group* group, int const idx,
                                gcs_node_state_t const check)
{
    bool ret = false;
    int i;
    for (i = 0; i < group->nodes_num; i++)
    {
        gcs_node_state_t state = group->nodes[i]->group.nodes[idx].status;
        if (check != state) {
            gu_error("At node %d node's %d status is not %d, but %d",
                     i, idx, check, state);
            ret = true;
        }
    }

    return ret;
}

/* start SST on behald of node idx (joiner) */
static long
group_sst_start (struct group* group, int const src_idx, const char* donor)
{
    ssize_t const req_len = strlen (donor) + 2;
    // leave one byte as sst request payload

    int donor_idx = -1;
    int i;
    for (i = 0; i < group->nodes_num; i++)
    {
        // sst request is expected to be dynamically allocated
        char* req_buf = malloc (req_len);
        fail_if (NULL == req_buf);
        sprintf (req_buf, "%s", donor);

        struct gcs_act_rcvd req = {
            .act = {.buf = req_buf, .buf_len = req_len,
                    .type = GCS_ACT_STATE_REQ },
            .sender_idx = src_idx,
            .id = GCS_SEQNO_ILL
        };

        long ret;

        ret = gcs_group_handle_state_request (&group->nodes[i]->group, &req);

        if (ret < 0) { // don't fail here, we may want to test negatives
            gu_error (ret < 0, "Handling state request to '%s' failed: %d (%s)",
                      donor, ret, strerror (-ret));
            return ret;
        }

        if (i == src_idx) {
            fail_if (ret != req_len);
            free (req_buf); // passed to joiner
        }
        else {
            if (ret > 0) {
                if (donor_idx < 0) {
                    fail_if (req.id != i);
                    donor_idx = i;
                    free (req_buf); // passed to donor
                }
                else {
                    fail ("More than one donor selected: %d, first donor: %d",
                          i, donor_idx);
                }
            }
        }
    }

    fail_if (donor_idx < 0, "Failed to select donor");

    for (i = 0; i < group->nodes_num; i++) {
        gcs_node_state_t state;
        gcs_group_t* gr = &group->nodes[i]->group;
        state = gr->nodes[donor_idx].status;
        fail_if (state != GCS_NODE_STATE_DONOR, "%d is not donor at %d",
                 donor_idx, i);
        state = gr->nodes[src_idx].status;
        fail_if (state != GCS_NODE_STATE_JOINER, "%d is not joiner at %d",
                 src_idx, i);

        /* check that donor and joiner point at each other */
        fail_if (memcmp (gr->nodes[donor_idx].joiner, gr->nodes[src_idx].id,
                         GCS_COMP_MEMB_ID_MAX_LEN+1),
                 "Donor points at wrong joiner: expected %s, got %s",
                 gr->nodes[src_idx].id, gr->nodes[donor_idx].joiner);

        fail_if (memcmp (gr->nodes[src_idx].donor, gr->nodes[donor_idx].id,
                         GCS_COMP_MEMB_ID_MAX_LEN+1),
                 "Joiner points at wrong donor: expected %s, got %s",
                 gr->nodes[donor_idx].id, gr->nodes[src_idx].donor);
    }

    return 0;
}

/* Thes test was specifically created to reproduce #465 */
START_TEST(gcs_memb_test_465)
{
    struct group group;
    group.nodes_num = 0;

    struct node nodes[MAX_NODES];
    int i;
    ssize_t ret = 0;

    // initialize individual node structures
    for (i = 0; i < MAX_NODES; i++) {
        int const str_len = 32;
        char name_str[str_len];
        char addr_str[str_len];

        sprintf(name_str, "node%d", i);
        sprintf(addr_str, "addr%d", i);
        gcs_group_init (&nodes[i].group, NULL, name_str, addr_str, 0, 0, 0);
    }

    gcs_node_state_t node_state;

    // bootstrap the cluster
    group_add_node (&group, &nodes[0], true);
    fail_if (nodes[0].group.state != GCS_GROUP_PRIMARY);
    node_state = get_node_state (&nodes[0]);
    fail_if (node_state != GCS_NODE_STATE_JOINED);

    deliver_join_sync_msg (&group, 0, GCS_MSG_SYNC);
    node_state = get_node_state (&nodes[0]);
    fail_if (node_state != GCS_NODE_STATE_SYNCED);

    group_add_node (&group, &nodes[1], true);
    fail_if (nodes[1].group.state != GCS_GROUP_PRIMARY);
    node_state = get_node_state (&nodes[1]);
    fail_if (node_state != GCS_NODE_STATE_PRIM); // need sst

    group_add_node (&group, &nodes[2], true);
    fail_if (nodes[2].group.state != GCS_GROUP_PRIMARY);
    node_state = get_node_state (&nodes[2]);
    fail_if (node_state != GCS_NODE_STATE_PRIM); // need sst

    fail_if (verify_node_state_across_group (&group, 0, GCS_NODE_STATE_SYNCED));

    group_sst_start (&group, 2, nodes[0].group.nodes[0].name);
    deliver_join_sync_msg (&group, 0, GCS_MSG_JOIN); // end of donor SST
    deliver_join_sync_msg (&group, 0, GCS_MSG_SYNC); // donor synced 
    deliver_join_sync_msg (&group, 2, GCS_MSG_SYNC); // joiner can't sync
    fail_if (verify_node_state_across_group (&group, 2, GCS_NODE_STATE_JOINER));
    deliver_join_sync_msg (&group, 2, GCS_MSG_JOIN); // end of joiner SST
    deliver_join_sync_msg (&group, 2, GCS_MSG_SYNC); // joiner synced

    fail_if (verify_node_state_across_group (&group, 0, GCS_NODE_STATE_SYNCED));
    fail_if (verify_node_state_across_group (&group, 1, GCS_NODE_STATE_PRIM));
    fail_if (verify_node_state_across_group (&group, 2, GCS_NODE_STATE_SYNCED));

    group_sst_start (&group, 1, nodes[0].group.nodes[0].name);
    deliver_join_sync_msg (&group, 0, GCS_MSG_JOIN); // end of donor SST
    deliver_join_sync_msg (&group, 1, GCS_MSG_JOIN); // end of joiner SST

    struct node* dropped = group_drop_node (&group, 1);
    fail_if (NULL == dropped);

    /* After that, according to #465, node 1 shifted from SYNCED to PRIMARY */

    fail_if (verify_node_state_across_group (&group, 1, GCS_NODE_STATE_SYNCED));
    struct gcs_act act;
    int            proto_ver = -1;
    ret = gcs_group_act_conf (&group.nodes[1]->group, &act, &proto_ver);
    fail_if (ret <= 0, "gcs_group_act_cnf() retruned %zd (%s)",
             ret, strerror (-ret));
    fail_if (ret != act.buf_len);
    fail_if (proto_ver != 0 /* current version */, "proto_ver = %d", proto_ver);
    const gcs_act_conf_t* conf = act.buf;
    fail_if (NULL == conf);
    fail_if (conf->my_idx != 1);
    /* according to #465 this was GCS_NODE_STATE_PRIM */
    fail_if (conf->my_state != GCS_NODE_STATE_SYNCED);

    deliver_join_sync_msg (&group, 0, GCS_MSG_SYNC); // donor synced
    fail_if (verify_node_state_across_group (&group, 0, GCS_NODE_STATE_SYNCED));
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

