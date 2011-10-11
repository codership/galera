/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <errno.h>

#include "gcs_group.h"
#include "gcs_gcache.h"

const char* gcs_group_state_str[GCS_GROUP_STATE_MAX] =
{
    "NON_PRIMARY",
    "WAIT_STATE_UUID",
    "WAIT_STATE_MSG",
    "PRIMARY"
};

long
gcs_group_init (gcs_group_t* group, gcache_t* const cache,
                const char* node_name, const char* inc_addr,
                gcs_proto_t const gcs_proto_ver, int const repl_proto_ver,
                int const appl_proto_ver)
{
    // here we also create default node instance.
    group->cache        = cache;
    group->act_id       = GCS_SEQNO_ILL;
    group->conf_id      = GCS_SEQNO_ILL;
    group->state_uuid   = GU_UUID_NIL;
    group->group_uuid   = GU_UUID_NIL;
    group->num          = 1; // this must be removed (#474)
    group->my_idx       = 0; // this must be -1 (#474)
    group->my_name      = strdup(node_name ? node_name : NODE_NO_NAME);
    group->my_address   = strdup(inc_addr  ? inc_addr  : NODE_NO_ADDR);
    group->state        = GCS_GROUP_NON_PRIMARY;
    group->last_applied = GCS_SEQNO_ILL; // mark for recalculation
    group->last_node    = -1;
    group->frag_reset   = true; // just in case
    group->nodes        = GU_CALLOC(group->num, gcs_node_t); // this must be removed (#474)

    if (!group->nodes) return -ENOMEM; // this should be removed (#474)

    /// this should be removed (#474)
    gcs_node_init (&group->nodes[group->my_idx], group->cache, NODE_NO_ID,
                   group->my_name, group->my_address, gcs_proto_ver,
                   repl_proto_ver, appl_proto_ver);

    group->prim_uuid  = GU_UUID_NIL;
    group->prim_seqno = GCS_SEQNO_ILL;
    group->prim_num   = 0;
    group->prim_state = GCS_NODE_STATE_NON_PRIM;

    *(gcs_proto_t*)&group->gcs_proto_ver = gcs_proto_ver;
    *(int*)&group->repl_proto_ver = repl_proto_ver;
    *(int*)&group->appl_proto_ver = appl_proto_ver;

    group->quorum = GCS_QUORUM_NON_PRIMARY;

    return 0;
}

long
gcs_group_init_history (gcs_group_t*     group,
                        gcs_seqno_t      seqno,
                        const gu_uuid_t* uuid)
{
    bool negative_seqno = (seqno < 0);
    bool nil_uuid = !gu_uuid_compare (uuid, &GU_UUID_NIL);

    if (negative_seqno && !nil_uuid) {
        gu_error ("Non-nil history UUID with negative seqno (%lld) makes "
                  "no sense.", (long long) seqno);
        return -EINVAL;
    }
    else if (!negative_seqno && nil_uuid) {
        gu_error ("Non-negative state seqno requires non-nil history UUID.");
        return -EINVAL;
    }
    group->act_id     = seqno;
    group->group_uuid = *uuid;
    return 0;
}

/* Initialize nodes array from component message */
static inline gcs_node_t*
group_nodes_init (const gcs_group_t* group, const gcs_comp_msg_t* comp)
{
    const long my_idx     = gcs_comp_msg_self (comp);
    const long nodes_num  = gcs_comp_msg_num  (comp);
    gcs_node_t* ret = GU_CALLOC (nodes_num, gcs_node_t);
    long i;

    if (ret) {
        for (i = 0; i < nodes_num; i++) {
            if (my_idx != i) {
                gcs_node_init (&ret[i], group->cache, gcs_comp_msg_id (comp, i),
                               NULL, NULL, -1, -1, -1);
            }
            else { // this node
                gcs_node_init (&ret[i], group->cache, gcs_comp_msg_id (comp, i),
                               group->my_name, group->my_address,
                               group->gcs_proto_ver, group->repl_proto_ver,
                               group->appl_proto_ver);
            }
        }
    }
    else {
        gu_error ("Could not allocate %ld x %z bytes", nodes_num,
                  sizeof(gcs_node_t));
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
    if (group->my_name)    free ((char*)group->my_name);
    if (group->my_address) free ((char*)group->my_address);
    group_nodes_free (group);
}

/* Reset nodes array without breaking the statistics */
static inline void
group_nodes_reset (gcs_group_t* group)
{
    register long i;
    /* reset recv_acts at the nodes */
    for (i = 0; i < group->num; i++) {
        if (i != group->my_idx) {
            gcs_node_reset (&group->nodes[i]);
        }
        else {
            gcs_node_reset_local (&group->nodes[i]);
        }
    }

    group->frag_reset = true;
}

/* Find node with the smallest last_applied */
static inline void
group_redo_last_applied (gcs_group_t* group)
{
    long       n;
    long       last_node    = -1;
    gu_seqno_t last_applied = GU_LONG_LONG_MAX;

    for (n = 0; n < group->num; n++) {
        gcs_node_t*      node  = &group->nodes[n];
        gcs_seqno_t      seqno = gcs_node_get_last_applied (node);

//        gu_debug ("last_applied[%ld]: %lld", n, seqno);

        /* NOTE: It is crucial for consistency that last_applied algorithm
         *       is absolutely identical on all nodes. Therefore for the 
         *       generality sake and future compatibility we have to assume
         *       non-blocking donor.
         *       GCS_BLOCKING_DONOR should never be defined unless in some
         *       very custom builds. Commenting it out for safety sake. */
//#ifndef GCS_BLOCKING_DONOR
        if ((GCS_NODE_STATE_SYNCED == node->status ||
             GCS_NODE_STATE_DONOR  == node->status)
//#else
//        if ((GCS_NODE_STATE_SYNCED == node->status) /* ignore donor */
//#endif
            && (seqno < last_applied)) {
            assert (seqno >= 0);
            last_applied = seqno;
            last_node    = n;
        }
    }

    if (gu_likely (last_node >= 0)) {
        group->last_applied = last_applied;
        group->last_node    = last_node;
    }
}

static void
group_go_non_primary (gcs_group_t* group)
{
    if (GCS_GROUP_PRIMARY == group->state) {
        group->nodes[group->my_idx].status = GCS_NODE_STATE_NON_PRIM;
        //@todo: Perhaps the same has to be applied to the rest of the nodes[]?
    }

    group->state   = GCS_GROUP_NON_PRIMARY;
    group->conf_id = GCS_SEQNO_ILL;
    // what else? Do we want to change anything about the node here?
}

static const char group_empty_id[GCS_COMP_MEMB_ID_MAX_LEN + 1] = { 0, };
 
static void
group_check_donor (gcs_group_t* group)
{
    gcs_node_state_t const my_state = group->nodes[group->my_idx].status;
    const char*      const donor_id = group->nodes[group->my_idx].donor;

    if (GCS_NODE_STATE_JOINER == my_state &&
        memcmp (donor_id, group_empty_id, sizeof(group_empty_id)))
    {
        long i;

        for (i = 0; i < group->num; i++)
        {
            if (i != group->my_idx &&
                !memcmp (donor_id, group->nodes[i].id,
                         sizeof (group->nodes[i].id)))
                return;
        }

        gu_warn ("Donor %s is no longer in the group. State transfer cannot "
                 "be completed, need to abort. Aborting...", donor_id);

        gu_abort();
    }

    return;
}

/*! Processes state messages and sets group parameters accordingly */
static void
group_post_state_exchange (gcs_group_t* group)
{
    const gcs_state_msg_t* states[group->num];
    gcs_state_quorum_t* quorum = &group->quorum;
    bool new_exchange = gu_uuid_compare (&group->state_uuid, &GU_UUID_NIL);
    long i;

    /* Collect state messages from nodes. */
    /* Looping here every time is suboptimal, but simply counting state messages
     * is not straightforward too: nodes may disappear, so the final count may
     * include messages from the disappeared nodes.
     * Let's put it this way: looping here is reliable and not that expensive.*/
    for (i = 0; i < group->num; i++) {
        states[i] = group->nodes[i].state_msg;
        if (NULL == states[i] ||
            (new_exchange &&
             gu_uuid_compare (&group->state_uuid,
                              gcs_state_msg_uuid(states[i]))))
            return; // not all states from THIS state exch. received, wait
    }
    gu_debug ("STATE EXCHANGE: "GU_UUID_FORMAT" complete.",
              GU_UUID_ARGS(&group->state_uuid));

    gcs_state_msg_get_quorum (states, group->num, quorum);

    // Update each node state based on quorum outcome:
    // is it up to date, does it need SST and stuff
    for (i = 0; i < group->num; i++) {
        gcs_node_update_status (&group->nodes[i], quorum);
    }

    if (quorum->primary) {
        // primary configuration
        if (new_exchange) {
            // new state exchange happened
            group->state      = GCS_GROUP_PRIMARY;
            group->act_id     = quorum->act_id;
            group->conf_id    = quorum->conf_id + 1;
            group->group_uuid = quorum->group_uuid;
            group->prim_uuid  = group->state_uuid;
            group->state_uuid = GU_UUID_NIL;
        }
        else {
            // no state exchange happend, processing old state messages
            assert (GCS_GROUP_PRIMARY == group->state);
            group->conf_id++;
        }

        group->prim_seqno = group->conf_id;
        group->prim_num   = 0;

        for (i = 0; i < group->num; i++) {
            group->prim_num += gcs_node_is_joined (group->nodes[i].status);
        }

        assert (group->prim_num > 0);
    }
    else {
        // non-primary configuration
        group_go_non_primary (group);
    }

    gu_info ("Quorum results:"
             "\n\tversion    = %u,"
             "\n\tcomponent  = %s,"
             "\n\tconf_id    = %lld,"
             "\n\tmembers    = %d/%d (joined/total),"
             "\n\tact_id     = %lld,"
             "\n\tlast_appl. = %lld,"
             "\n\tprotocols  = %d/%d/%d (gcs/repl/appl),"
             "\n\tgroup UUID = "GU_UUID_FORMAT,
             quorum->version,
             quorum->primary ? "PRIMARY" : "NON-PRIMARY",
             quorum->conf_id,
             group->prim_num, group->num,
             quorum->act_id,
             group->last_applied,
             quorum->gcs_proto_ver, quorum->repl_proto_ver,
             quorum->appl_proto_ver,
             GU_UUID_ARGS(&quorum->group_uuid));

    group_check_donor(group);
}

// does basic sanity check of the component message (in response to #145)
static void
group_check_comp_msg (bool prim, long my_idx, long members)
{
    if (my_idx >= 0) {
        if (my_idx < members) return;
    }
    else {
        if (!prim && (0 == members)) return;
    }

    gu_fatal ("Malformed component message from backend: "
              "%s, idx = %ld, members = %ld",
              prim ? "PRIMARY" : "NON-PRIMARY", my_idx, members);

    assert (0);
    gu_abort ();
}

gcs_group_state_t
gcs_group_handle_comp_msg (gcs_group_t* group, const gcs_comp_msg_t* comp)
{
    long        new_idx, old_idx;
    gcs_node_t* new_nodes = NULL;
    ulong       new_memb  = 0;

    const bool prim_comp     = gcs_comp_msg_primary(comp);
    const long new_my_idx    = gcs_comp_msg_self   (comp);
    const long new_nodes_num = gcs_comp_msg_num    (comp);

    group_check_comp_msg (prim_comp, new_my_idx, new_nodes_num);

    if (new_my_idx >= 0) {
        gu_info ("New COMPONENT: primary = %s, my_idx = %ld, memb_num = %ld",
                 prim_comp ? "yes" : "no", new_my_idx, new_nodes_num);

        new_nodes = group_nodes_init (group, comp);

        if (!new_nodes) {
            gu_fatal ("Could not allocate memory for %ld-node component.",
                      gcs_comp_msg_num (comp));
            assert(0);
            return -ENOMEM;
        }

        if (GCS_GROUP_PRIMARY == group->state) {
            gu_debug ("#281: Saving %s over %s",
                      gcs_node_state_to_str(group->nodes[group->my_idx].status),
                      gcs_node_state_to_str(group->prim_state));
            group->prim_state = group->nodes[group->my_idx].status;
        }
    }
    else {
        // Self-leave message
        gu_info ("Received self-leave message.");
        assert (0 == new_nodes_num);
        assert (!prim_comp);
    }

    if (prim_comp) {
        /* Got PRIMARY COMPONENT - Hooray! */
        assert (new_my_idx >= 0);
        if (group->state == GCS_GROUP_PRIMARY) {
            /* we come from previous primary configuration, relax */
        }
        else {
            const bool first_component =
#ifndef GCS_CORE_TESTING
            (1 == group->num) && !strcmp (NODE_NO_ID, group->nodes[0].id);
#else
            (1 == group->num);
#endif

            if (1 == new_nodes_num && first_component) {
                /* bootstrap new configuration */
                assert (GCS_GROUP_NON_PRIMARY == group->state);
                assert (1 == group->num);
                assert (0 == group->my_idx);

                // This bootstraps initial primary component for state exchange
                gu_uuid_generate (&group->prim_uuid, NULL, 0);
                group->prim_seqno = 0;
                group->prim_num   = 1;
                group->state      = GCS_GROUP_PRIMARY;

                if (group->act_id < 0) {
                    // no history provided: start a new one
                    group->act_id  = GCS_SEQNO_NIL;
                    gu_uuid_generate (&group->group_uuid, NULL, 0);
                    gu_info ("Starting new group from scratch: "GU_UUID_FORMAT,
                             GU_UUID_ARGS(&group->group_uuid));
                }
// the following should be removed under #474
                group->nodes[0].status = GCS_NODE_STATE_JOINED;
                /* initialize node ID to the one given by the backend - this way
                 * we'll be recognized as coming from prev. conf. in node array
                 * remap below */
                strncpy ((char*)group->nodes[0].id, new_nodes[0].id,
                         sizeof (new_nodes[0].id) - 1);
            }
        }
    }
    else {
        group_go_non_primary (group);
    }

    /* Remap old node array to new one to preserve action continuity */
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

    group->my_idx = new_my_idx;
    group->num    = new_nodes_num;
    group->nodes  = new_nodes;

    if (gcs_comp_msg_primary(comp)) {
        /* TODO: for now pretend that we always have new nodes and perform
         * state exchange because old states can carry outdated node status.
         * (also protocol voting needs to be redone)
         * However this means aborting ongoing actions. Find a way to avoid
         * this extra state exchange. Generate new state messages on behalf
         * of other nodes? see #238 */
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

    if (GCS_GROUP_WAIT_STATE_UUID == group->state &&
        0 == msg->sender_idx /* check that it is from the representative */) {
        group->state_uuid = *(gu_uuid_t*)msg->buf;
        group->state      = GCS_GROUP_WAIT_STATE_MSG;
    }
    else {
        gu_warn ("Stray state UUID msg: "GU_UUID_FORMAT
                 " from node %ld (%s), current group state %s",
                 GU_UUID_ARGS((gu_uuid_t*)msg->buf),
                 msg->sender_idx, group->nodes[msg->sender_idx].name,
                 gcs_group_state_str[group->state]);
    }

    return group->state;
}

static void group_print_state_debug(gcs_state_msg_t* state)
{
    size_t str_len = 1024;
    char state_str[str_len];
    gcs_state_msg_snprintf (state_str, str_len, state);
    gu_info ("%s", state_str);
}

gcs_group_state_t
gcs_group_handle_state_msg (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    if (GCS_GROUP_WAIT_STATE_MSG == group->state) {

        gcs_state_msg_t* state = gcs_state_msg_read (msg->buf, msg->size);

        if (state) {

            const gu_uuid_t* state_uuid = gcs_state_msg_uuid (state);

            if (!gu_uuid_compare(&group->state_uuid, state_uuid)) {

                gu_info ("STATE EXCHANGE: got state msg: "GU_UUID_FORMAT
                         " from %ld (%s)", GU_UUID_ARGS(state_uuid),
                         msg->sender_idx, gcs_state_msg_name(state));

                if (gu_log_debug) group_print_state_debug(state);

                gcs_node_record_state (&group->nodes[msg->sender_idx], state);
                group_post_state_exchange (group);
            }
            else {
                gu_debug ("STATE EXCHANGE: stray state msg: "GU_UUID_FORMAT
                          " from node %ld (%s), current state UUID: "
                          GU_UUID_FORMAT,
                          GU_UUID_ARGS(state_uuid),
                          msg->sender_idx, gcs_state_msg_name(state),
                          GU_UUID_ARGS(&group->state_uuid));

                if (gu_log_debug) group_print_state_debug(state);

                gcs_state_msg_destroy (state);
            }
        }
        else {
            gu_warn ("Could not parse state message from node %d",
                     msg->sender_idx, group->nodes[msg->sender_idx].name);
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

        if (old_val < group->last_applied) return group->last_applied;
    }

    return 0;
}

/*! return true if this node is the sender to notify the calling thread of
 * success */
long
gcs_group_handle_join_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    long const  sender_idx = msg->sender_idx;
    gcs_node_t* sender     = &group->nodes[sender_idx];

    assert (GCS_MSG_JOIN == msg->type);

    // TODO: define an explicit type for the join message, like gcs_join_msg_t
    assert (msg->size == sizeof(gcs_seqno_t));

    if (GCS_NODE_STATE_DONOR  == sender->status ||
        GCS_NODE_STATE_JOINER == sender->status) {
        long j;
        gcs_seqno_t seqno     = gcs_seqno_le(*(gcs_seqno_t*)msg->buf);
        gcs_node_t* peer      = NULL;
        const char* peer_id   = NULL;
        const char* peer_name = "left the group";
        long        peer_idx  = -1;
        bool        from_donor = false;
        const char* st_dir    = NULL; // state transfer direction symbol

        if (GCS_NODE_STATE_DONOR == sender->status) {
            peer_id    = sender->joiner;
            from_donor = true;
            st_dir     = "to";
            /* #454 - we don't switch to JOINED here, 
             *        instead going straignt to SYNCED */
            // sender->status = GCS_NODE_STATE_JOINED;
        }
        else {
            peer_id = sender->donor;
            st_dir  = "from";
            if (seqno >= 0) {
                sender->status = GCS_NODE_STATE_JOINED;
                group->prim_num++;
            }
        }

        // Try to find peer.
        for (j = 0; j < group->num; j++) {
            if (j == sender_idx) continue;
            if (!memcmp(peer_id, group->nodes[j].id,
                        sizeof (group->nodes[j].id))) {
                peer_idx  = j;
                peer      = &group->nodes[peer_idx];
                peer_name = peer->name;
                break;
            }
        }

        if (j == group->num) {
            gu_warn ("Could not find peer: %s", peer_id);
        }

        if (seqno < 0) {
            gu_warn ("%ld (%s): State transfer %s %ld (%s) failed: %d (%s)",
                     sender_idx, sender->name, st_dir, peer_idx, peer_name,
                     (int)seqno, strerror((int)-seqno));

            if (from_donor && peer_idx == group->my_idx &&
                GCS_NODE_STATE_JOINER == group->nodes[peer_idx].status) {
                // this node will be waiting for SST forever. If it has only
                // one recv thread there is no (generic) way to wake it up.
                gu_fatal ("Will never receive state. Need to abort.");
                // return to core to shutdown the backend before aborting
                return -ENOTRECOVERABLE;
            }
        }
        else {
            gu_info ("%ld (%s): State transfer %s %ld (%s) complete.",
                     sender_idx, sender->name, st_dir, peer_idx, peer_name);
        }
    }
    else {
        if (GCS_NODE_STATE_PRIM == sender->status) {
            gu_warn ("Rejecting JOIN message from %ld (%s): new State Transfer"
                     " required.", sender_idx, sender->name);
        }
        else {
            // should we freak out and throw an error?
            gu_warn ("Protocol violation. JOIN message sender %ld (%s) is not "
                     "in state transfer (%s). Message ignored.", sender_idx,
                     sender->name, gcs_node_state_to_str(sender->status));
        }
        return 0;
    }

    return (sender_idx == group->my_idx);
}

long
gcs_group_handle_sync_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    long        sender_idx = msg->sender_idx;
    gcs_node_t* sender     = &group->nodes[sender_idx];

    assert (GCS_MSG_SYNC == msg->type);

    if (GCS_NODE_STATE_JOINED == sender->status ||
        /* #454 - at this layer we jump directly from DONOR to SYNCED */
        GCS_NODE_STATE_DONOR  == sender->status) {

        sender->status = GCS_NODE_STATE_SYNCED;

        group_redo_last_applied (group);//from now on this node must be counted

        gu_info ("Member %ld (%s) synced with group.",
                 sender_idx, sender->name);

        return (sender_idx == group->my_idx);
    }
    else {
        if (GCS_NODE_STATE_SYNCED != sender->status) {
            gu_warn ("SYNC message sender from non-joined %ld (%s). Ignored.",
                     msg->sender_idx, sender->name);
        }
        else {
            gu_debug ("Redundant SYNC message from %ld (%s).",
                      msg->sender_idx, sender->name);
        }
        return 0;
    }
}

static long
group_find_node_by_name (gcs_group_t* group, long joiner_idx, const char* name,
                         gcs_node_state_t status)
{
    long idx;

    for (idx = 0; idx < group->num; idx++) {
        gcs_node_t* node = &group->nodes[idx];
        if (!strcmp(node->name, name)) {
            if (joiner_idx == idx) {
                return -EHOSTDOWN;
            }
            else if (node->status >= status) {
                return idx;
            }
            else {
                return -EAGAIN;
            }
        }
    }

    return -EHOSTUNREACH;
}

static long
group_find_node_by_state (gcs_group_t* group, gcs_node_state_t status)
{
    long idx;

    for (idx = 0; idx < group->num; idx++) {
        gcs_node_t* node = &group->nodes[idx];
        if (status == node->status &&
            strcmp (node->name, GCS_ARBITRATOR_NAME)) // avoid arbitrator
            return idx;
    }

    return -EAGAIN;
}

/*!
 * Selects and returns the index of state transfer donor, if available.
 * Updates donor and joiner status if state transfer is possible
 *
 * @return
 *         donor index or negative error code:
 *         -EHOSTUNREACH if reqiested donor is not available
 *         -EAGAIN       if there were no nodes in the proper state.
 */
static long
group_select_donor (gcs_group_t* group, long const joiner_idx,
                    const char* const donor_name)
{
    static gcs_node_state_t const min_donor_state = GCS_NODE_STATE_SYNCED;

    long donor_idx;
    bool required_donor = (strlen(donor_name) > 0);

    if (required_donor) {
        donor_idx = group_find_node_by_name (group, joiner_idx, donor_name,
                                             min_donor_state);
    }
    else {
        donor_idx = group_find_node_by_state (group, min_donor_state);
    }

    if (donor_idx >= 0) {
        gcs_node_t* const joiner = &group->nodes[joiner_idx];
        gcs_node_t* const donor  = &group->nodes[donor_idx];

        assert(donor_idx != joiner_idx);

        gu_info ("Node %ld (%s) requested state transfer from '%s'. "
                 "Selected %ld (%s)(%s) as donor.",
                 joiner_idx, joiner->name, required_donor ? donor_name :"*any*",
                 donor_idx, donor->name, gcs_node_state_to_str(donor->status));

        // reserve donor, confirm joiner
        donor->status  = GCS_NODE_STATE_DONOR;
        joiner->status = GCS_NODE_STATE_JOINER;
        memcpy (donor->joiner, joiner->id, GCS_COMP_MEMB_ID_MAX_LEN+1);
        memcpy (joiner->donor, donor->id,  GCS_COMP_MEMB_ID_MAX_LEN+1);
    }
    else {
#if 0
        gu_warn ("Node %ld (%s) requested state transfer from '%s', "
                 "but it is impossible to select State Transfer donor: %s",
                 joiner_idx, group->nodes[joiner_idx].name,
                 required_donor ? donor_name : "*any*", strerror (-donor_idx));
#endif
    }

    return donor_idx;
}

/* Cleanup ignored state request */
void
gcs_group_ignore_action (gcs_group_t* group, struct gcs_act_rcvd* act)
{
    if (act->act.type <= GCS_ACT_STATE_REQ) {
        gcs_gcache_free (group->cache, act->act.buf);
    }

    act->act.buf     = NULL;
    act->act.buf_len = 0;
    act->act.type    = GCS_ACT_ERROR;
    act->sender_idx  = -1;
    assert (GCS_SEQNO_ILL == act->id);
}

/* NOTE: check gcs_request_state_transfer() for sender part. */
/*! Returns 0 if request is ignored, request size if it should be passed up */
long
gcs_group_handle_state_request (gcs_group_t*         group,
                                struct gcs_act_rcvd* act)
{
    // pass only to sender and to one potential donor
    const char*      donor_name     = act->act.buf;
    size_t           donor_name_len = strlen(donor_name);
    long             donor_idx      = -1;
    long const       joiner_idx     = act->sender_idx;
    const char*      joiner_name    = group->nodes[joiner_idx].name;
    gcs_node_state_t joiner_status  = group->nodes[joiner_idx].status;

    assert (GCS_ACT_STATE_REQ == act->act.type);

    if (joiner_status != GCS_NODE_STATE_PRIM) {

        const char* joiner_status_string = gcs_node_state_to_str(joiner_status);

        if (group->my_idx == joiner_idx) {
            gu_error ("Requesting state transfer while in %s. "
                      "Ignoring.", joiner_status_string);
            act->id = -ECANCELED;
            return act->act.buf_len;
        }
        else {
            gu_error ("Node %ld (%s) requested state transfer, "
                      "but its state is %s. Ignoring.",
                      joiner_idx, joiner_name, joiner_status_string);
            gcs_group_ignore_action (group, act);
            return 0;
        }
    }

    donor_idx = group_select_donor(group, joiner_idx, donor_name);

    assert (donor_idx != joiner_idx);

    if (group->my_idx != joiner_idx && group->my_idx != donor_idx) {
        // if neither DONOR nor JOINER, ignore request
        gcs_group_ignore_action (group, act);
        return 0;
    }
    else if (group->my_idx == donor_idx) {
        act->act.buf_len -= donor_name_len + 1;
        memmove (*(void**)&act->act.buf,
                 act->act.buf + donor_name_len+1,
                 act->act.buf_len);
        // now action starts with request, like it was supplied by application,
        // see gcs_request_state_transfer()
    }

    // Return index of donor (or error) in the seqno field to sender.
    // It will be used to detect error conditions (no availabale donor,
    // donor crashed and the like).
    // This may be ugly, well, any ideas?
    act->id = donor_idx;

    return act->act.buf_len;
}

static ssize_t
group_memb_record_size (gcs_group_t* group)
{
    ssize_t ret = 0;
    long idx;

    for (idx = 0; idx < group->num; idx++) {
        ret += strlen(group->nodes[idx].id) + 1;
        ret += strlen(group->nodes[idx].name) + 1;
        ret += strlen(group->nodes[idx].inc_addr) + 1;
    }

    return ret;
}

/* Creates new configuration action */
ssize_t
gcs_group_act_conf (gcs_group_t*    group,
                    struct gcs_act* act,
                    int*            gcs_proto_ver)
{
    if (*gcs_proto_ver < group->quorum.gcs_proto_ver)
        *gcs_proto_ver = group->quorum.gcs_proto_ver; // only go up, see #482
    else if (group->quorum.gcs_proto_ver >= 0 &&
             group->quorum.gcs_proto_ver < *gcs_proto_ver) {
        gu_warn ("Refusing GCS protocol version downgrade from %d to %d",
                 *gcs_proto_ver, group->quorum.gcs_proto_ver);
    }

    ssize_t conf_size = sizeof(gcs_act_conf_t) + group_memb_record_size(group);
    gcs_act_conf_t* conf = malloc (conf_size);

    if (conf) {
        long idx;

        conf->seqno          = group->act_id;
        conf->conf_id        = group->conf_id;
        conf->memb_num       = group->num;
        conf->my_idx         = group->my_idx;
        conf->repl_proto_ver = group->quorum.repl_proto_ver;
        conf->appl_proto_ver = group->quorum.appl_proto_ver;

        memcpy (conf->uuid, &group->group_uuid, sizeof (gu_uuid_t));

        if (group->num) {
            assert (conf->my_idx >= 0);
#if 0
#ifndef GCS_FOR_GARB
            /* See #395 - if we're likely to need SST, clear up outdated
             * seqno map */
            if ((group->conf_id >= 0) &&
                (group->nodes[group->my_idx].status < GCS_NODE_STATE_JOINER) &&
                NULL != group->cache) {
                gcache_seqno_init (group->cache, conf->seqno);
            }
#endif
#endif
            conf->my_state = group->nodes[group->my_idx].status;

            char* ptr = &conf->data[0];
            for (idx = 0; idx < group->num; idx++)
            {
                strcpy (ptr, group->nodes[idx].id);
                ptr += strlen(ptr) + 1;
                strcpy (ptr, group->nodes[idx].name);
                ptr += strlen(ptr) + 1;
                strcpy (ptr, group->nodes[idx].inc_addr);
                ptr += strlen(ptr) + 1;
            }
        }
        else {
            // self leave message
            assert (conf->conf_id < 0);
            assert (conf->my_idx  < 0);
            conf->my_state = GCS_NODE_STATE_NON_PRIM;
        }

        act->buf     = conf;
        act->buf_len = conf_size;
        act->type    = GCS_ACT_CONF;

        return conf_size;
    }
    else {
        return -ENOMEM;
    }
}

// for future use in fake state exchange (in unit tests et.al. See #237, #238)
static gcs_state_msg_t*
group_get_node_state (gcs_group_t* group, long node_idx)
{
    const gcs_node_t* node = &group->nodes[node_idx];

    uint8_t flags = 0;

    if (0 == node_idx) flags |= GCS_STATE_FREP;

    return gcs_state_msg_create (
        &group->state_uuid,
        &group->group_uuid,
        &group->prim_uuid,
        group->prim_num,
        group->prim_seqno,
        group->act_id,
        group->prim_state,
        node->status,
        node->name,
        node->inc_addr,
        node->gcs_proto_ver,
        node->repl_proto_ver,
        node->appl_proto_ver,
        flags
        );
}

/*! Returns state message object for this node */
gcs_state_msg_t*
gcs_group_get_state (gcs_group_t* group)
{
    return group_get_node_state (group, group->my_idx);
}

