/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <errno.h>

#include "gcs_group.h"

static const char* group_state_str[GCS_GROUP_STATE_MAX] =
{
    "GCS_GROUP_NON_PRIMARY",
    "GCS_GROUP_WAIT_STATE_UUID",
    "GCS_GROUP_WAIT_STATE_MSG",
    "GCS_GROUP_PRIMARY"
};

long
gcs_group_init (gcs_group_t* group)
{
    // here we also create default node instance.
    group->act_id       = 0;
    group->conf_id      = GCS_SEQNO_ILL;
    group->state_uuid   = GU_UUID_NIL;
    group->group_uuid   = GU_UUID_NIL;
    group->proto        = -1;
    group->num          = 1;
    group->my_idx       = 0;
    group->state        = GCS_GROUP_NON_PRIMARY;
    group->last_applied = GCS_SEQNO_ILL; // mark for recalculation
    group->last_node    = -1;
    group->nodes        =  GU_CALLOC(group->num, gcs_node_t);

    if (!group->nodes) return -ENOMEM;

    gcs_node_init (&group->nodes[group->my_idx], "No ID");

    return 0;
}

/* Initialize nodes array from component message */
static inline gcs_node_t*
group_nodes_init (const gcs_comp_msg_t* comp)
{
    gcs_node_t* ret = NULL;
    long nodes_num = gcs_comp_msg_num (comp);
    register long i;

    ret = GU_CALLOC (nodes_num, gcs_node_t);
    if (ret) {
        for (i = 0; i < nodes_num; i++) {
            gcs_node_init (&ret[i], gcs_comp_msg_id (comp, i));
        }
    }
    return ret;
}

/* Free nodes array */
static inline void
group_nodes_free (gcs_group_t* group)
{
    register long i;

    /* cleanup after disappeared members */
    for (i = 0; i < group->num; i++) {
        gcs_node_free (&group->nodes[i]);
    }

    if (group->nodes) gu_free (group->nodes);
}

void
gcs_group_free (gcs_group_t* group)
{
    group_nodes_free (group);
}

/* Reset nodes array without breaking the statistics */
static inline void
group_nodes_reset (gcs_group_t* group)
{
    register long i;
    /* reset recv_acts at the nodes */
    for (i = 0; i < group->num; i++) {
        gcs_node_reset (&group->nodes[i]);
    }
}

/* Find node with the smallest last_applied */
static inline void
group_redo_last_applied (gcs_group_t* group)
{
    long n;

    group->last_node    = 0;
    group->last_applied = gcs_node_get_last_applied (&group->nodes[0]);
//    gu_debug (" last_applied[0]: %lld", group->last_applied);

    for (n = 1; n < group->num; n++) {
        gcs_seqno_t seqno = gcs_node_get_last_applied (&group->nodes[n]);
//        gu_debug ("last_applied[%ld]: %lld", n, seqno);
        if (seqno < group->last_applied) {
            group->last_applied = seqno;
            group->last_node    = n;
        }
    }
}

static void
group_go_non_primary (gcs_group_t* group)
{
    group->state   = GCS_GROUP_NON_PRIMARY;
    group->conf_id = GCS_SEQNO_ILL;
    // what else? Do we want to change anything about the node here?
    // Probably we should keep old node status until next configuration
    // change.
}

/*! Processes state messages and sets group parameters accordingly */
static void
group_post_state_exchange (gcs_group_t* group)
{
    const gcs_state_t* states[group->num];
    gcs_state_quorum_t quorum;
    long i;

    /* Looping here every time is suboptimal, but simply counting state messages
     * is not straightforward too: nodes may disappear, so the final count may
     * inlcude messages from the disappeared nodes.
     * Let's put it this way: looping here is reliable and not that expensive.*/
    for (i = 0; i < group->num; i++) {
        if (group->nodes[i].state)
            states[i] = group->nodes[i].state;
        else
            return; // not all states received, wait more
    }

    gu_debug ("STATE EXCHANGE: "GU_UUID_FORMAT" complete.",
              GU_UUID_ARGS(&group->state_uuid));

    gcs_state_get_quorum (states, group->num, &quorum);

    if (quorum.primary) {
        // primary configuration
        group->proto = quorum.proto;
        if (gu_uuid_compare (&group->state_uuid, &GU_UUID_NIL)) {
            // new state exchange happened
            group->state      = GCS_GROUP_PRIMARY;
            group->act_id     = quorum.act_id;
            group->conf_id    = quorum.conf_id + 1;
            group->group_uuid = quorum.group_uuid;
            group->state_uuid = GU_UUID_NIL;

            // Update each node state based on quorum outcome:
            // is it up to date, does it need SST and stuff
            for (i = 0; i < group->num; i++) {
                gcs_node_update_status (&group->nodes[i], &quorum);
            }
        }
        else {
            // no state exchange happend, processing old state messages
            assert (GCS_GROUP_PRIMARY == group->state);
            group->conf_id++;
        }
    }
    else {
        // non-primary configuration
        group_go_non_primary (group);
    }

    gu_debug ("Quorum results:"
              "\n\t%s,"
              "\n\tact_id     = %lld,"
              "\n\tconf_id    = %lld,"
              "\n\tlast_appl. = %lld,"
              "\n\tprotocol   = %hd,"
              "\n\tgroup UUID = "GU_UUID_FORMAT,
              quorum.primary ? "PRIMARY" : "NON-PRIMARY",
              group->act_id, group->conf_id, group->last_applied, group->proto,
              GU_UUID_ARGS(&quorum.group_uuid));
}

gcs_group_state_t
gcs_group_handle_comp_msg (gcs_group_t* group, const gcs_comp_msg_t* comp)
{
    long        new_idx, old_idx;
    long        new_nodes_num = 0;
    gcs_node_t *new_nodes = NULL;
    ulong       new_memb = 0;

    gu_debug ("primary = %s, my_id = %d, memb_num = %d",
	      gcs_comp_msg_primary(comp) ? "yes" : "no",
	      gcs_comp_msg_self(comp), gcs_comp_msg_num (comp));

    if (gcs_comp_msg_primary(comp)) {
	/* Got PRIMARY COMPONENT - Hooray! */
	/* create new nodes array according to new membrship */
	new_nodes_num = gcs_comp_msg_num (comp);
	new_nodes     = group_nodes_init (comp);
	if (!new_nodes) return -ENOMEM;
	
	if (group->state == GCS_GROUP_PRIMARY) {
	    /* we come from previous primary configuration */
	}
	else {
            if (1 == new_nodes_num && 0 == group->act_id &&
                GCS_SEQNO_ILL == group->conf_id) {
                /* First node in the group. Generate new group UUID */
                assert (GCS_GROUP_NON_PRIMARY == group->state);
                assert (1 == group->num);
                assert (0 == group->my_idx);
                gu_uuid_generate (&group->group_uuid, NULL, 0);
                group->conf_id = 0; // this bootstraps configuration ID
                group->state   = GCS_GROUP_PRIMARY;
                group->nodes[group->my_idx].status = GCS_STATE_JOINED;
                /* initialize node ID to the one given by the backend - this way
                 * we'll be recognized as coming from prev. conf. below */
                strncpy ((char*)group->nodes[group->my_idx].id, new_nodes[0].id,
                         sizeof (new_nodes[0].id) - 1);
                /* forge own state message - for group_post_state_exchange() */
                gcs_node_record_state (&group->nodes[group->my_idx],
                                       gcs_group_get_state (group));
                gu_info ("Starting new group: " GU_UUID_FORMAT,
                         GU_UUID_ARGS(&group->group_uuid));
            }
            else {
                /* It happened so that we missed some primary configurations */
                gu_warn ("Discontinuity in primary configurations!");
                gu_warn ("State snapshot is needed!");
            }
            /* Move information about this node to new node array */
        }
    }
    else {
	/* Got NON-PRIMARY COMPONENT - cleanup */
        /* All sending threads must be aborted with -ENOTCONN,
         * local action FIFO must be flushed. Not implemented: FIXME! */
        group_go_non_primary (group);
    }

    /* remap old array to new one to preserve action continuity */
    assert (group->nodes);
    for (new_idx = 0; new_idx < new_nodes_num; new_idx++) {
        /* find member index in old component by unique member id */
        for (old_idx = 0; old_idx < group->num; old_idx++) {
            // just scan through old group
            if (!strcmp(group->nodes[old_idx].id, new_nodes[new_idx].id)) {
                /* the node was in previous configuration with us */
                /* move node context to new node array */
                gcs_node_move (&new_nodes[new_idx], &group->nodes[old_idx]);
                break;
            }
        }
        /* if wasn't found in new configuration, new member -
         * need to do state exchange */
        new_memb |= (old_idx == group->num);
    }

    /* free old nodes array */
    group_nodes_free (group);

    group->nodes  = new_nodes;
    group->num    = new_nodes_num;
    group->my_idx = gcs_comp_msg_self (comp);

    if (gcs_comp_msg_primary(comp)) {
        /* FIXME: for now pretend that we always have new nodes and perform
         * state exchange because old states can carry outdated node status.
         * However this means aborting ongoing actions. Find a way to avoid
         * this extra state exchange. */
        new_memb = true;
        /* if new nodes joined, reset ongoing actions and state messages */
        if (new_memb) {
            group_nodes_reset (group);
            group->state      = GCS_GROUP_WAIT_STATE_UUID;
            group->state_uuid = GU_UUID_NIL; // prepare for state exchange
        }
        else {
            if (GCS_GROUP_PRIMARY == group->state) {
                /* since we don't have any new nodes since last PRIMARY,
                   we skip state exchange */
                group_post_state_exchange (group);
            }
        }
        group_redo_last_applied (group);
    }

    return group->state;
}

gcs_group_state_t
gcs_group_handle_uuid_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    assert (msg->size == sizeof(gu_uuid_t));

    if (GCS_GROUP_WAIT_STATE_UUID == group->state) {
        group->state_uuid = *(gu_uuid_t*)msg->buf;
        group->state      = GCS_GROUP_WAIT_STATE_MSG;
    }
    else {
        gu_debug ("Stray state UUID msg: "GU_UUID_FORMAT
                  " from node %d, current group state %s",
                  GU_UUID_ARGS(&group->state_uuid), msg->sender_id,
                  group_state_str[group->state]);
    }

    return group->state;
}

gcs_group_state_t
gcs_group_handle_state_msg (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    if (GCS_GROUP_WAIT_STATE_MSG == group->state) {
        gcs_state_t* state = gcs_state_msg_read (msg->buf, msg->size);

        if (state) {
            const gu_uuid_t* state_uuid = gcs_state_uuid (state);

            if (!gu_uuid_compare(&group->state_uuid, state_uuid)) {
                gu_info ("STATE EXCHANGE: got state msg: "GU_UUID_FORMAT
                         " from %d",
                         GU_UUID_ARGS(state_uuid), msg->sender_id);
                gcs_node_record_state (&group->nodes[msg->sender_id], state);
                group_post_state_exchange (group);
            }
            else {
                gu_debug ("STATE EXCHANGE: stray state msg: "GU_UUID_FORMAT
                          "from node %d, current state UUID: "GU_UUID_FORMAT,
                          GU_UUID_ARGS(state_uuid), msg->sender_id,
                          GU_UUID_ARGS(&group->state_uuid));
                gcs_state_destroy (state);
            }
        }
        else {
            gu_warn ("Could not parse state message from node %d",
                     msg->sender_id);
        }
    }

    return group->state;
}

/*! Returns new last applied value if it has changes, 0 otherwise */
gcs_seqno_t
gcs_group_handle_last_msg (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    gcs_seqno_t seqno;

    assert (GCS_MSG_LAST        == msg->type);
    assert (sizeof(gcs_seqno_t) == msg->size);

    seqno = gcs_seqno_le(*(gcs_seqno_t*)(msg->buf));

    // This assert is too restrictive. It requires application to send
    // last applied messages while holding TO, otherwise there's a race
    // between threads.
    // assert (seqno >= group->last_applied);

    gcs_node_set_last_applied (&group->nodes[msg->sender_id], seqno);

    if (msg->sender_id == group->last_node && seqno > group->last_applied) {
        /* node that was responsible for the last value, has changed it.
         * need to recompute it */
        gcs_seqno_t old_val = group->last_applied;
        group_redo_last_applied (group);
        if (old_val != group->last_applied) {
            return group->last_applied;
        }
    }

    return 0;
}

ssize_t
gcs_group_act_conf (gcs_group_t* group, gcs_recv_act_t* act)
{
    ssize_t conf_size = sizeof(gcs_act_conf_t) + group->num*GCS_MEMBER_NAME_MAX;
    gcs_act_conf_t* conf = malloc (conf_size);
    if (conf) {
        conf->seqno    = group->act_id;
        conf->conf_id  = group->conf_id;
        conf->memb_num = group->num;
        conf->my_idx   = group->my_idx;

        act->buf  = conf;
        act->type = GCS_ACT_CONF;
        return conf_size;
    }
    else {
        return -ENOMEM;
    }
}

/*! Returns state object for state message */
extern gcs_state_t*
gcs_group_get_state (gcs_group_t* group) {
    const gcs_node_t* my_node = &group->nodes[group->my_idx];
    return gcs_state_create (
        &group->state_uuid,
        &group->group_uuid,
        group->act_id,
        group->conf_id,
        my_node->status,
        my_node->name,
        my_node->inc_addr,
        my_node->proto_min,
        my_node->proto_max
        );
}

