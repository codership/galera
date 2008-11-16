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
    group->act_id       = GCS_SEQNO_ILL;
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

long
gcs_group_init_history (gcs_group_t*     group,
                        gcs_seqno_t      seqno,
                        const gu_uuid_t* uuid)
{
    group->act_id     = seqno;
    group->group_uuid = *uuid;
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
    bool new_exchange = gu_uuid_compare (&group->state_uuid, &GU_UUID_NIL);
    long i;

    /* Collect state messages from nodes. */
    /* Looping here every time is suboptimal, but simply counting state messages
     * is not straightforward too: nodes may disappear, so the final count may
     * include messages from the disappeared nodes.
     * Let's put it this way: looping here is reliable and not that expensive.*/
    for (i = 0; i < group->num; i++) {
        states[i] = group->nodes[i].state;
        if (NULL == states[i] ||
            (new_exchange &&
             gu_uuid_compare (&group->state_uuid, gcs_state_uuid(states[i]))))
            return; // not all states from THIS state exch. received, wait more
    }
    gu_debug ("STATE EXCHANGE: "GU_UUID_FORMAT" complete.",
              GU_UUID_ARGS(&group->state_uuid));

    gcs_state_get_quorum (states, group->num, &quorum);

    if (quorum.primary) {
        // primary configuration
        group->proto = quorum.proto;
        if (new_exchange) {
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

    group->my_idx = gcs_comp_msg_self (comp);
    new_nodes_num = gcs_comp_msg_num  (comp);

    gu_debug ("primary = %s, my_id = %d, memb_num = %d",
	      gcs_comp_msg_primary(comp) ? "yes" : "no",
	      group->my_idx, new_nodes_num);

    if (gcs_comp_msg_primary(comp)) {
	/* Got PRIMARY COMPONENT - Hooray! */
	/* create new nodes array according to new membrship */
        assert (new_nodes_num);
	new_nodes = group_nodes_init (comp);
	if (!new_nodes) return -ENOMEM;
	
	if (group->state == GCS_GROUP_PRIMARY) {
	    /* we come from previous primary configuration */
	}
	else {
            if (1 == new_nodes_num && GCS_SEQNO_ILL == group->conf_id) {
                /* First configuration for this process, only node in group:
                 * bootstrap new configuration by default. */
                assert (GCS_GROUP_NON_PRIMARY == group->state);
                assert (1 == group->num);
                assert (0 == group->my_idx);

                group->conf_id = 0; // this bootstraps configuration ID
                group->state   = GCS_GROUP_PRIMARY;

                if (GCS_SEQNO_ILL == group->act_id) {
                    // no history provided: start a new one
                    group->act_id  = GCS_SEQNO_NIL;
                    gu_uuid_generate (&group->group_uuid, NULL, 0);
                    gu_info ("Starting new group from scratch: "GU_UUID_FORMAT,
                             GU_UUID_ARGS(&group->group_uuid));
                }

                group->nodes[0].status = GCS_STATE_JOINED;
                /* initialize node ID to the one given by the backend - this way
                 * we'll be recognized as coming from prev. conf. below */
                strncpy ((char*)group->nodes[0].id, new_nodes[0].id,
                         sizeof (new_nodes[0].id) - 1);
                /* forge own state message - for group_post_state_exchange() */
                gcs_node_record_state (&group->nodes[0],
                                       gcs_group_get_state (group));
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
        if (group->my_idx < 0) {
            // Self-leave message
            assert (0 == new_nodes_num);
            new_nodes = NULL;
            gu_info ("Received self-leave message.");
        }
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

    if (gcs_comp_msg_primary(comp)) {
        /* FIXME: for now pretend that we always have new nodes and perform
         * state exchange because old states can carry outdated node status.
         * However this means aborting ongoing actions. Find a way to avoid
         * this extra state exchange. Generate new state messages on behalf
         * of other nodes? */
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
                  GU_UUID_ARGS(&group->state_uuid), msg->sender_idx,
                  group_state_str[group->state]);
    }

    return group->state;
}

static void group_print_state_debug(gcs_state_t* state)
{
    size_t str_len = 1024;
    char state_str[str_len];
    gcs_state_snprintf (state_str, str_len, state);
    gu_debug ("%s", state_str);
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
                         GU_UUID_ARGS(state_uuid), msg->sender_idx);
                if (gu_log_debug) group_print_state_debug(state);

                gcs_node_record_state (&group->nodes[msg->sender_idx], state);
                group_post_state_exchange (group);
            }
            else {
                gu_debug ("STATE EXCHANGE: stray state msg: "GU_UUID_FORMAT
                          "from node %d, current state UUID: "GU_UUID_FORMAT,
                          GU_UUID_ARGS(state_uuid), msg->sender_idx,
                          GU_UUID_ARGS(&group->state_uuid));
                if (gu_log_debug) group_print_state_debug(state);

                gcs_state_destroy (state);
            }
        }
        else {
            gu_warn ("Could not parse state message from node %d",
                     msg->sender_idx);
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

    gcs_node_set_last_applied (&group->nodes[msg->sender_idx], seqno);

    if (msg->sender_idx == group->last_node && seqno > group->last_applied) {
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

long
gcs_group_handle_join_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    long        sender_idx = msg->sender_idx;
    gcs_node_t* sender     = &group->nodes[sender_idx];

    assert (GCS_MSG_JOIN == msg->type);

    // TODO: define an explicit type for the join message, like gcs_join_msg_t
    assert (msg->size == sizeof(gcs_seqno_t));

    if (GCS_STATE_DONOR  == sender->status ||
        GCS_STATE_JOINER == sender->status) {
        long j;
        gcs_seqno_t seqno     = gu_le64(*(gcs_seqno_t*)msg->buf);
        gcs_node_t* peer      = NULL;
        const char* peer_id   = NULL;
        const char* peer_name = "left the group";
        long        peer_idx  = -1;
        const char* st_dir    = NULL; // state transfer direction symbol

        if (GCS_STATE_DONOR == sender->status) {
            peer_id = sender->joiner;
            st_dir  = ">>>";
            sender->status = GCS_STATE_JOINED;
        }
        else {
            peer_id = sender->donor;
            st_dir  = "<<<";
            if (seqno >= 0) sender->status = GCS_STATE_JOINED;
        }

        // Try to find peer.
        for (j = 0; j < group->num; j++) {
            if (j == sender_idx) continue;
            if (!memcmp(peer_id, group->nodes[j].id,
                        sizeof (group->nodes[j].id))) {
                peer_idx  = j;
                peer      = &group->nodes[peer_idx];
                peer_name = peer->name;
            }
        }

        if (seqno < 0) {
            gu_warn ("State Transfer %ld(%s) %s %ld(%s) failed: %d (%s)",
                     sender_idx, sender->name, st_dir, peer_idx, peer_name,
                     (int)seqno, strerror((int)-seqno));
        }
        else {
            gu_info ("State Transfer %ld(%s) %s %ld(%s) complete.",
                     sender_idx, sender->name, st_dir, peer_idx, peer_name);
        }

        return (sender_idx == group->my_idx);
    }
    else {
        if (GCS_STATE_PRIM == sender->status) {
            gu_warn ("Rejecting JOIN message: new State Transfer required.");
        }
        else {
            // should we freak out and return an error?
            gu_warn ("Protocol violation. JOIN message sender %ld is not "
                     "in state transfer (%s). Message ignored.",
                     msg->sender_idx, gcs_state_node_string[sender->status]);
            assert (0);
        }
        return 0;
    }
}

long
gcs_group_handle_sync_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    long        sender_idx = msg->sender_idx;
    gcs_node_t* sender     = &group->nodes[sender_idx];

    assert (GCS_MSG_SYNC == msg->type);

    if (GCS_STATE_JOINED == sender->status) {

        sender->status = GCS_STATE_SYNCED;

        gu_info ("Member %ld (%s) synced with group.",
                 sender_idx, sender->name);

        return (sender_idx == group->my_idx);
    }
    else {
        if (GCS_STATE_SYNCED != sender->status) {
            gu_warn ("Protocol violation. SYNC message sender %ld (%s) "
                     "is not joined. Message ignored.",
                     msg->sender_idx, sender->name);
            assert (0);
        }
        else {
            gu_debug ("Redundant SYNC message from %ld (%s).",
                      msg->sender_idx, sender->name);
        }
        return 0;
    }
}

static long
group_find_node_by_status (gcs_group_t* group, gcs_state_node_t status)
{
    long idx;
    for (idx = 0; idx < group->num; idx++) {
        gcs_node_t* node = &group->nodes[idx];
        if (status == node->status) return idx;
    }
    return -1;
}

/*!
 * Selects and returns the index of state transfer donor, if available.
 * Updates donor and joiner status if state transfer is possible
 *
 * @return
 *         donor index or negative error code. -1 in case of lack
 *         of available donors
 */
static long
group_select_donor (gcs_group_t* group, long joiner_idx)
{
    long idx;
    gcs_node_t* joiner = &group->nodes[joiner_idx];
    gcs_node_t* donor;

    // first, check SYNCED, they are able to process state request immediately
    idx = group_find_node_by_status (group, GCS_STATE_SYNCED);
    if (idx < 0) {
        // then check simply JOINED
        idx = group_find_node_by_status (group, GCS_STATE_JOINED);
        //assert (idx >= 0);
        if (idx < 0) return -EAGAIN;
    }

    assert(idx != joiner_idx);

    // reserve donor, confirm joiner
    donor = &group->nodes[idx];
    donor->status  = GCS_STATE_DONOR;
    joiner->status = GCS_STATE_JOINER;
    memcpy (donor->joiner, joiner->id, GCS_COMP_MEMB_ID_MAX_LEN+1);
    memcpy (joiner->donor, donor->id,  GCS_COMP_MEMB_ID_MAX_LEN+1);

    return idx;
}

extern long
gcs_group_handle_state_request (gcs_group_t*    group,
                                long            joiner_idx,
                                gcs_recv_act_t* act)
{
    // pass only to sender and to one potential donor
    long donor_idx;

    assert (GCS_ACT_STATE_REQ == act->type);

    if (group->nodes[joiner_idx].status != GCS_STATE_PRIM) {
        if (group->my_idx == joiner_idx) {
            gu_error ("Requesting state transfer while joined. "
                      "Ignoring.");
            act->id = -ECANCELED;
            return act->buf_len;
        }
        else {
            gu_error ("Node %ld requested state transfer, "
                      "but it is joined already.", joiner_idx);
            free ((void*)act->buf);
            return 0;
        }
    }

    donor_idx = group_select_donor(group, joiner_idx);
    assert (donor_idx != joiner_idx);

    if (donor_idx >= 0) {
        gu_info ("Node %ld requested State Transfer. "
                 "Selected %ld as donor.", joiner_idx, donor_idx);
    }
    else {
        gu_warn ("Node %ld requested State Transfer. "
                 "However failed to select State Transfer donor.");
    }

    if (group->my_idx != joiner_idx && group->my_idx != donor_idx) {
        // if neither DONOR nor JOINER, ignore message
        free ((void*)act->buf);
        return 0;
    }

    // Return index of donor (or error) in the seqno field to sender.
    // It will be used to detect error conditions (no availabale donor,
    // donor crashed and the like).
    // This may be ugly, well, any ideas?
    act->id = donor_idx;

    return act->buf_len;
}

ssize_t
gcs_group_act_conf (gcs_group_t* group, gcs_recv_act_t* act)
{
    ssize_t conf_size = sizeof(gcs_act_conf_t) + group->num*GCS_MEMBER_NAME_MAX;
    gcs_act_conf_t* conf = malloc (conf_size);

    if (conf) {
        long idx;

        conf->seqno       = group->act_id;
        conf->conf_id     = group->conf_id;
        conf->memb_num    = group->num;
        conf->my_idx      = group->my_idx;
        memcpy (conf->group_uuid, &group->group_uuid, sizeof (gu_uuid_t));

        if (group->num) {
            assert (conf->my_idx >= 0);
            conf->st_required =
                (group->nodes[group->my_idx].status < GCS_STATE_DONOR);

            for (idx = 0; idx < group->num; idx++)
            {
                char* node_id = &conf->data[idx * GCS_MEMBER_NAME_MAX];
                strncpy (node_id, group->nodes[idx].id, GCS_MEMBER_NAME_MAX);
                node_id[GCS_MEMBER_NAME_MAX - 1] = '\0';
            }
        }
        else {
            // leave message
            assert (conf->conf_id < 0);
            assert (conf->my_idx < 0);
            conf->st_required = false;
        }

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

