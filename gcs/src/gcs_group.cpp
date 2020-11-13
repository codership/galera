/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_group.hpp"
#include "gcs_gcache.hpp"
#include "gcs_priv.hpp"

#include <errno.h>

const char* gcs_group_state_str[GCS_GROUP_STATE_MAX] =
{
    "NON_PRIMARY",
    "WAIT_STATE_UUID",
    "WAIT_STATE_MSG",
    "PRIMARY"
};

int
gcs_group_init (gcs_group_t* group, gcache_t* const cache,
                const char* node_name, const char* inc_addr,
                gcs_proto_t const gcs_proto_ver, int const repl_proto_ver,
                int const appl_proto_ver)
{
    // here we also create default node instance.
    group->cache        = cache;
    group->act_id_      = GCS_SEQNO_ILL;
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
                   repl_proto_ver, appl_proto_ver, 0);

    group->prim_uuid  = GU_UUID_NIL;
    group->prim_seqno = GCS_SEQNO_ILL;
    group->prim_num   = 0;
    group->prim_state = GCS_NODE_STATE_NON_PRIM;
    group->prim_gcs_ver  = 0;
    group->prim_repl_ver = 0;
    group->prim_appl_ver = 0;

    *(gcs_proto_t*)&group->gcs_proto_ver = gcs_proto_ver;
    *(int*)&group->repl_proto_ver = repl_proto_ver;
    *(int*)&group->appl_proto_ver = appl_proto_ver;

    group->quorum = GCS_QUORUM_NON_PRIMARY;

    group->last_applied_proto_ver = -1;

    return 0;
}

int
gcs_group_init_history (gcs_group_t*     group,
                        gcs_seqno_t      seqno,
                        const gu_uuid_t* uuid)
{
    bool const negative_seqno(seqno < 0);
    bool const nil_uuid(!gu_uuid_compare (uuid, &GU_UUID_NIL));

    if (negative_seqno && !nil_uuid) {
        gu_error ("Non-nil history UUID with negative seqno (%lld) makes "
                  "no sense.", (long long) seqno);
        return -EINVAL;
    }
    else if (!negative_seqno && nil_uuid) {
        gu_error ("Non-negative state seqno requires non-nil history UUID.");
        return -EINVAL;
    }

    group->act_id_    = seqno;
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
            const gcs_comp_memb_t* memb = gcs_comp_msg_member(comp, i);
            assert(NULL != memb);

            if (my_idx != i) {
                gcs_node_init (&ret[i], group->cache, memb->id,
                               NULL, NULL, -1, -1, -1, memb->segment);
            }
            else { // this node
                gcs_node_init (&ret[i], group->cache, memb->id,
                               group->my_name, group->my_address,
                               group->gcs_proto_ver, group->repl_proto_ver,
                               group->appl_proto_ver, memb->segment);
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
#ifndef GCS_CORE_TESTING
static
#endif // GCS_CORE_TESTING
void
group_nodes_free (gcs_group_t* group)
{
    int i;

    /* cleanup after disappeared members */
    for (i = 0; i < group->num; i++) {
        gcs_node_free (&group->nodes[i]);
    }

    if (group->nodes) gu_free (group->nodes);

    group->nodes  = NULL;
    group->num    = 0;
    group->my_idx = -1;
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
    int i;
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
    gu_seqno_t last_applied = GU_LLONG_MAX;

    for (n = 0; n < group->num; n++) {
        const gcs_node_t* const node = &group->nodes[n];
        gcs_seqno_t const seqno = node->last_applied;
        bool count = node->count_last_applied;

        if (gu_unlikely (0 == group->last_applied_proto_ver)) {
            /* @note: this may be removed after quorum v1 is phased out */
            count = (GCS_NODE_STATE_SYNCED == node->status ||
                     GCS_NODE_STATE_DONOR  == node->status);
        }

//        gu_debug("redo_last_applied[%ld]: %lld, count: %s",
//                 n, seqno, count ? "yes" : "no");

        /* NOTE: It is crucial for consistency that last_applied algorithm
         *       is absolutely identical on all nodes. Therefore for the
         *       generality sake and future compatibility we have to assume
         *       non-blocking donor.
         *       GCS_BLOCKING_DONOR should never be defined unless in some
         *       very custom builds. Commenting it out for safety sake. */
//#ifndef GCS_BLOCKING_DONOR
        if (count
//#else
//        if ((GCS_NODE_STATE_SYNCED == node->status) /* ignore donor */
//#endif
            && (seqno < last_applied)) {
            assert (seqno >= 0);
            last_applied = seqno;
            last_node    = n;
        }
        // extra diagnostic, ignore
        //else if (!count) { gu_warn("not counting %d", n); }
    }

    if (gu_likely (last_node >= 0)) {
        group->last_applied = last_applied;
        group->last_node    = last_node;
    }
}

static void
group_go_non_primary (gcs_group_t* group)
{
    if (group->my_idx >= 0) {
        assert(group->num > 0);
        assert(group->nodes);

        group->nodes[group->my_idx].status = GCS_NODE_STATE_NON_PRIM;
        //@todo: Perhaps the same has to be applied to the rest of the nodes[]?
    }
    else {
        assert(-1   == group->my_idx);
        assert(0    == group->num);
        assert(NULL == group->nodes);
    }

    group->state   = GCS_GROUP_NON_PRIMARY;
    group->conf_id = GCS_SEQNO_ILL;
    // what else? Do we want to change anything about the node here?
}

static void
group_check_proto_ver(gcs_group_t* group)
{
    assert(group->quorum.primary); // must be called only on primary CC

    gcs_node_t& node(group->nodes[group->my_idx]);
    bool fail(false);

#define GROUP_CHECK_NODE_PROTO_VER(LEVEL)                               \
    if (node.LEVEL < group->quorum.LEVEL) {                             \
        gu_fatal("Group requested %s: %d, max supported by this node: %d." \
                 "Upgrade the node before joining this group."          \
                 "Need to abort.",                                      \
                 #LEVEL, group->quorum.LEVEL, node.LEVEL);              \
        fail = true;                                                    \
    }

    GROUP_CHECK_NODE_PROTO_VER(gcs_proto_ver);
    GROUP_CHECK_NODE_PROTO_VER(repl_proto_ver);
    GROUP_CHECK_NODE_PROTO_VER(appl_proto_ver);

#undef GROUP_CHECK_NODE_PROTO_VER

    if (fail) gu_abort();
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
    gu_debug ("STATE EXCHANGE: " GU_UUID_FORMAT " complete.",
              GU_UUID_ARGS(&group->state_uuid));

    gcs_state_msg_get_quorum (states, group->num, quorum);

    if (quorum->version >= 0) {
        if (quorum->version < 2) {
            group->last_applied_proto_ver = 0;
        }
        else {
            group->last_applied_proto_ver = 1;
        }
    }
    else {
        gu_fatal ("Negative quorum version: %d", quorum->version);
        gu_abort();
    }

    // Update each node state based on quorum outcome:
    // is it up to date, does it need SST and stuff
    for (i = 0; i < group->num; i++) {
        gcs_node_update_status (&group->nodes[i], quorum);
    }

    if (quorum->primary) {
        // primary configuration
        if (new_exchange) {
            // new state exchange happened
            if (!gu_uuid_compare(&group->group_uuid, &quorum->group_uuid) &&
                group->act_id_ > quorum->act_id)
            {
                gu_fatal("Reversing history: %lld -> %lld, this member has "
                         "applied %lld more events than the primary component."
                         "Data loss is possible. Must abort.",
                         (long long)group->act_id_, (long long)quorum->act_id,
                         (long long)(group->act_id_ - quorum->act_id));
                group->state  = GCS_GROUP_INCONSISTENT;
                return;
            }
            group->state      = GCS_GROUP_PRIMARY;
            group->act_id_    = quorum->act_id;
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

#define GROUP_UPDATE_PROTO_VER(LEVEL) \
        if (group->prim_##LEVEL##_ver < quorum->LEVEL##_proto_ver) \
            group->prim_##LEVEL##_ver = quorum->LEVEL##_proto_ver;
        GROUP_UPDATE_PROTO_VER(gcs);
        GROUP_UPDATE_PROTO_VER(repl);
        GROUP_UPDATE_PROTO_VER(appl);
#undef GROUP_UPDATE_PROTO_VER
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
             "\n\tgroup UUID = " GU_UUID_FORMAT,
             quorum->version,
             quorum->primary ? "PRIMARY" : "NON-PRIMARY",
             quorum->conf_id,
             group->prim_num, group->num,
             quorum->act_id,
             group->last_applied,
             quorum->gcs_proto_ver, quorum->repl_proto_ver,
             quorum->appl_proto_ver,
             GU_UUID_ARGS(&quorum->group_uuid));

    if (quorum->primary) group_check_proto_ver(group);
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

    const bool prim_comp     = gcs_comp_msg_primary  (comp);
    const bool bootstrap     = gcs_comp_msg_bootstrap(comp);
    const long new_my_idx    = gcs_comp_msg_self     (comp);
    const long new_nodes_num = gcs_comp_msg_num      (comp);

    group_check_comp_msg (prim_comp, new_my_idx, new_nodes_num);

    if (new_my_idx >= 0) {
        gu_info ("New COMPONENT: primary = %s, bootstrap = %s, my_idx = %ld, "
                 "memb_num = %ld", prim_comp ? "yes" : "no",
                 bootstrap ? "yes" : "no", new_my_idx, new_nodes_num);

        new_nodes = group_nodes_init (group, comp);

        if (!new_nodes) {
            gu_fatal ("Could not allocate memory for %ld-node component.",
                      gcs_comp_msg_num (comp));
            assert(0);
            return (gcs_group_state_t)-ENOMEM;
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
        else if (bootstrap)
        {
            /* Is there need to initialize something else in this case? */
            group->nodes[group->my_idx].bootstrap = true;
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

                if (group->act_id_ < 0) {
                    // no history provided: start a new one
                    group->act_id_ = GCS_SEQNO_NIL;
                    gu_uuid_generate (&group->group_uuid, NULL, 0);
                    gu_info ("Starting new group from scratch: " GU_UUID_FORMAT,
                             GU_UUID_ARGS(&group->group_uuid));
                }
// the following should be removed under #474
                group->nodes[0].status = GCS_NODE_STATE_JOINED;
                /* initialize node ID to the one given by the backend - this way
                 * we'll be recognized as coming from prev. conf. in node array
                 * remap below */
                strncpy (group->nodes[0].id, new_nodes[0].id,
                         sizeof (group->nodes[0].id));
                group->nodes[0].segment = new_nodes[0].segment;
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

    if (gcs_comp_msg_primary(comp) || bootstrap) {
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
//        group->state_uuid = *(gu_uuid_t*)msg->buf;
        gu_uuid_copy(&group->state_uuid, (const gu_uuid_t*)msg->buf);
        group->state = GCS_GROUP_WAIT_STATE_MSG;
    }
    else {
        gu_warn ("Stray state UUID msg: " GU_UUID_FORMAT
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

                gu_info ("STATE EXCHANGE: got state msg: " GU_UUID_FORMAT
                         " from %d (%s)", GU_UUID_ARGS(state_uuid),
                         msg->sender_idx, gcs_state_msg_name(state));

                if (gu_log_debug) group_print_state_debug(state);

                gcs_node_record_state (&group->nodes[msg->sender_idx], state);
                group_post_state_exchange (group);
            }
            else {
                gu_debug ("STATE EXCHANGE: stray state msg: " GU_UUID_FORMAT
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

    seqno = gcs_seqno_gtoh(*(gcs_seqno_t*)(msg->buf));

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

        if (old_val < group->last_applied) {
            gu_debug ("New COMMIT CUT %lld after %lld from %d",
                      (long long)group->last_applied,
                      (long long)seqno, msg->sender_idx);
            return group->last_applied;
        }
    }

    return 0;
}

/*! return true if this node is the sender to notify the calling thread of
 * success */
int
gcs_group_handle_join_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    int const   sender_idx = msg->sender_idx;
    gcs_node_t* sender    = &group->nodes[sender_idx];

    assert (GCS_MSG_JOIN == msg->type);

    // TODO: define an explicit type for the join message, like gcs_join_msg_t
    assert (msg->size == sizeof(gcs_seqno_t));

    if (GCS_NODE_STATE_DONOR  == sender->status ||
        GCS_NODE_STATE_JOINER == sender->status) {
        int j;
        gcs_seqno_t seqno     = gcs_seqno_gtoh(*(gcs_seqno_t*)msg->buf);
        gcs_node_t* peer      = NULL;
        const char* peer_id   = NULL;
        const char* peer_name = "left the group";
        int         peer_idx  = -1;
        bool        from_donor = false;
        const char* st_dir    = NULL; // state transfer direction symbol

        if (GCS_NODE_STATE_DONOR == sender->status) {
            peer_id    = sender->joiner;
            from_donor = true;
            st_dir     = "to";

            assert (group->last_applied_proto_ver >= 0);

            if (0 == group->last_applied_proto_ver) {
                /* #454 - we don't switch to JOINED here,
                 *        instead going straignt to SYNCED */
            }
            else {
                assert(sender->count_last_applied);
                assert(sender->desync_count > 0);
                sender->desync_count -= 1;
                if (0 == sender->desync_count)
                    sender->status = GCS_NODE_STATE_JOINED;
            }
        }
        else {
            peer_id = sender->donor;
            st_dir  = "from";

            if (group->quorum.version < 2) {
                // #591 remove after quorum v1 is phased out
                sender->status = GCS_NODE_STATE_JOINED;
                group->prim_num++;
            }
            else {
                if (seqno >= 0) {
                    sender->status = GCS_NODE_STATE_JOINED;
                    group->prim_num++;
                }
                else {
                    sender->status = GCS_NODE_STATE_PRIM;
                }
            }
        }

        // Try to find peer.
        for (j = 0; j < group->num; j++) {
// #483            if (j == sender_idx) continue;
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
            gu_warn ("%d.%d (%s): State transfer %s %d.%d (%s) failed: %d (%s)",
                     sender_idx, sender->segment, sender->name, st_dir,
                     peer_idx, peer ? peer->segment : -1, peer_name,
                     (int)seqno, strerror((int)-seqno));

            if (from_donor && peer_idx == group->my_idx &&
                GCS_NODE_STATE_JOINER == group->nodes[peer_idx].status) {
                // this node will be waiting for SST forever. If it has only
                // one recv thread there is no (generic) way to wake it up.
                gu_fatal ("Will never receive state. Need to abort.");
                // return to core to shutdown the backend before aborting
                return -ENOTRECOVERABLE;
            }

            if (group->quorum.version < 2 && !from_donor && // #591
                sender_idx == group->my_idx) {
                // remove after quorum v1 is phased out
                gu_fatal ("Faield to receive state. Need to abort.");
                return -ENOTRECOVERABLE;
            }
        }
        else {
            if (GCS_NODE_STATE_JOINED == sender->status) {
                if (sender_idx == peer_idx) {
                    gu_info("Member %d.%d (%s) resyncs itself to group.",
                            sender_idx, sender->segment, sender->name);
                }
                else {
                    gu_info("%d.%d (%s): State transfer %s %d.%d (%s) complete.",
                            sender_idx, sender->segment, sender->name, st_dir,
                            peer_idx, peer ? peer->segment : -1, peer_name);
                }
            }
            else {
                assert(sender->desync_count > 0);
                return 0; // don't deliver up
            }
        }
    }
    else {
        if (GCS_NODE_STATE_PRIM == sender->status) {
            gu_warn("Rejecting JOIN message from %d.%d (%s): new State Transfer"
                    " required.", sender_idx, sender->segment, sender->name);
        }
        else {
            // should we freak out and throw an error?
            gu_warn("Protocol violation. JOIN message sender %d.%d (%s) is not "
                    "in state transfer (%s). Message ignored.",
                    sender_idx, sender->segment, sender->name,
                    gcs_node_state_to_str(sender->status));
        }
        return 0;
    }

    return (sender_idx == group->my_idx);
}

int
gcs_group_handle_sync_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    int const   sender_idx = msg->sender_idx;
    gcs_node_t* sender     = &group->nodes[sender_idx];

    assert (GCS_MSG_SYNC == msg->type);

    if (GCS_NODE_STATE_JOINED == sender->status ||
        /* #454 - at this layer we jump directly from DONOR to SYNCED */
        (0 == group->last_applied_proto_ver &&
         GCS_NODE_STATE_DONOR == sender->status)) {

        sender->status = GCS_NODE_STATE_SYNCED;
        sender->count_last_applied = true;

        group_redo_last_applied (group);//from now on this node must be counted

        gu_info ("Member %d.%d (%s) synced with group.",
                 sender_idx, sender->segment, sender->name);

        return (sender_idx == group->my_idx);
    }
    else {
        if (GCS_NODE_STATE_SYNCED == sender->status) {
            gu_debug ("Redundant SYNC message from %d.%d (%s).",
                      sender_idx, sender->segment, sender->name);
        }
        else if (GCS_NODE_STATE_DONOR == sender->status) {
            // this is possible with quick succession of desync()/resync() calls
            gu_debug ("SYNC message from %d.%d (%s, DONOR). Ignored.",
                      sender_idx, sender->segment, sender->name);
        }
        else {
            gu_warn ("SYNC message from non-JOINED %d.%d (%s, %s). Ignored.",
                     sender_idx, sender->segment, sender->name,
                     gcs_node_state_to_str(sender->status));
        }

        /* signal sender that it didn't work */
        return -ERESTART * (sender_idx == group->my_idx);
    }
}

static inline bool
group_node_is_stateful (const gcs_group_t* group, const gcs_node_t* node)
{
    if (group->quorum.version < 3) {
        return strcmp (node->name, GCS_ARBITRATOR_NAME);
    }
    else {
        return ((gcs_node_flags(node) & GCS_STATE_ARBITRATOR) == 0);
    }
}

static int
group_find_node_by_state (const gcs_group_t*     const group,
                          int              const joiner_idx,
                          gcs_node_state_t const status)
{
    gcs_segment_t const segment = group->nodes[joiner_idx].segment;
    int  idx;
    int  donor = -1;
    bool hnss = false; /* have nodes in the same segment */

    for (idx = 0; idx < group->num; idx++) {

        if (joiner_idx == idx) continue; /* skip joiner */

        gcs_node_t* node = &group->nodes[idx];

        if (node->status >= status && group_node_is_stateful (group, node))
            donor = idx; /* potential donor */

        if (segment == node->segment) {
            if (donor == idx) return donor; /* found suitable donor in the
                                             * same segment */
            if (node->status >= GCS_NODE_STATE_JOINER) hnss = true;;
        }
    }

    /* Have not found suitable donor in the same segment. */
    if (!hnss && donor >= 0) {
        if (joiner_idx == group->my_idx) {
            gu_warn ("There are no nodes in the same segment that will ever "
                     "be able to become donors, yet there is a suitable donor "
                     "outside. Will use that one.");
        }
        return donor;
    }
    else {
        /* wait for a suitable donor to appear in the same segment */
        return -EAGAIN;
    }
}

static int
group_find_node_by_name (const gcs_group_t* const group, int const joiner_idx,
                         const char* const name, int const name_len,
                         gcs_node_state_t const status)
{
    int idx;

    for (idx = 0; idx < group->num; idx++) {
        gcs_node_t* node = &group->nodes[idx];
        if (!strncmp(node->name, name, name_len)) {
            if (joiner_idx == idx) {
                return -EHOSTDOWN;
            }
            else if (node->status >= status) {
                return idx;
            }
            else if (node->status >= GCS_NODE_STATE_JOINER) {
                /* will eventually become SYNCED */
                return -EAGAIN;
            }
            else {
                /* technically we could return -EDEADLK here, but as long as
                 * it is not -EAGAIN, it does not matter. If the node is in a
                 * PRIMARY state, it is as good as not found. */
                break;
            }
        }
    }

    return -EHOSTUNREACH;
}

/* Calls group_find_node_by_name() for each name in comma-separated list,
 * falls back to group_find_node_by_state() if name (or list) is empty. */
static int
group_for_each_donor_in_string (const gcs_group_t* const group,
                                int const str_version,
                                int const joiner_idx,
                                const char* const str, int const str_len,
                                gcs_node_state_t const status)
{
    assert (str != NULL);

    const char* begin = str;
    const char* end;
    int err = -EHOSTDOWN; /* worst error */
    /* dangling comma */
    bool const dcomma = (str_len && str[str_len-1] == ',' &&
                         str_version >= 2);

    do {
        end = strchr(begin, ',');

        int len;

        if (NULL == end) {
            len = str_len - (begin - str);
        }
        else {
            len = end - begin;
        }

        assert (len >= 0);

        int idx;
        if (len > 0) {
            idx = group_find_node_by_name (group, joiner_idx, begin, len,
                                           status);
        }
        else {
            if (err == -EAGAIN && !dcomma) {
               /* -EAGAIN here means that at least one of the nodes in the
                * list will be available later, so don't try others.
                * (Proto 1 UPDATE: unless there is a dangling comma) */
                idx = err;
            }
            else {
                idx = group_find_node_by_state(group, joiner_idx, status);
            }
        }

        if (idx >= 0) return idx;

        /* once we hit -EAGAIN, don't try to change error code: this means
         * that at least one of the nodes in the list will become available. */
        if (-EAGAIN != err) err = idx;

        begin = end + 1; /* skip comma */

    } while (end != NULL);

    return err;
}

static gcs_seqno_t
group_lowest_cached_seqno(const gcs_group_t* const group)
{
    gcs_seqno_t ret = GCS_SEQNO_ILL;
    int idx = 0;
    for (idx = 0; idx < group->num; idx++) {
        gcs_seqno_t seq = gcs_node_cached(&group->nodes[idx]);
        if (seq != GCS_SEQNO_ILL)
        {
            if (ret == GCS_SEQNO_ILL ||
                seq < ret)
            {
                ret = seq;
            }
        }
    }
    return ret;
}

static int
group_find_ist_donor_by_name (const gcs_group_t* const group,
                              int joiner_idx,
                              const char* name, int  name_len,
                              gcs_seqno_t ist_seqno,
                              gcs_node_state_t status)
{
    int idx = 0;
    for (idx = 0; idx < group->num; idx++)
    {
        gcs_node_t* node = &group->nodes[idx];
        gcs_seqno_t cached = gcs_node_cached(node);
        if (strncmp(node->name, name, name_len) == 0 &&
            joiner_idx != idx &&
            node->status >= status &&
            cached != GCS_SEQNO_ILL &&
            // ist potentially possible
            (ist_seqno + 1) >= cached)
        {
            return idx;
        }
    }
    return -1;
}

static int
group_find_ist_donor_by_name_in_string (
    const gcs_group_t* const group,
    int joiner_idx,
    const char* str, int str_len,
    gcs_seqno_t ist_seqno,
    gcs_node_state_t status)
{
    assert (str != NULL);

    const char* begin = str;
    const char* end;

    gu_debug("ist_seqno[%lld]", (long long)ist_seqno);
    // return the highest cached seqno node.
    int ret = -1;
    do {
        end = strchr(begin, ',');
        int len = 0;
        if (end == NULL) {
            len = str_len - (begin - str);
        } else {
            len = end - begin;
        }
        assert (len >= 0);
        if (len == 0) break;
        int idx = group_find_ist_donor_by_name(
            group, joiner_idx, begin, len,
            ist_seqno, status);
        if (idx >= 0)
        {
            if (ret == -1 ||
                gcs_node_cached(&group->nodes[idx]) >=
                gcs_node_cached(&group->nodes[ret]))
            {
                ret = idx;
            }
        }
        begin = end + 1;
    } while (end != NULL);

    if (ret == -1) {
        gu_debug("not found");
    } else {
        gu_debug("found. name[%s], seqno[%lld]",
                 group->nodes[ret].name,
                 (long long)gcs_node_cached(&group->nodes[ret]));
    }
    return ret;
}

static int
group_find_ist_donor_by_state (const gcs_group_t* const group,
                               int joiner_idx,
                               gcs_seqno_t ist_seqno,
                               gcs_node_state_t status)
{
    gcs_node_t* joiner = &group->nodes[joiner_idx];
    gcs_segment_t joiner_segment = joiner->segment;

    // find node who is ist potentially possible.
    // first highest cached seqno local node.
    // then highest cached seqno remote node.
    int idx = 0;
    int local_idx = -1;
    int remote_idx = -1;
    for (idx = 0; idx < group->num; idx++)
    {
        if (joiner_idx == idx) continue;

        gcs_node_t* const node = &group->nodes[idx];
        gcs_seqno_t const node_cached = gcs_node_cached(node);

        if (node->status >= status &&
            group_node_is_stateful(group, node) &&
            node_cached != GCS_SEQNO_ILL &&
            node_cached <= (ist_seqno + 1))
        {
            int* const idx_ptr =
                (joiner_segment == node->segment) ? &local_idx : &remote_idx;

            if (*idx_ptr == -1 ||
                node_cached >= gcs_node_cached(&group->nodes[*idx_ptr]))
            {
                *idx_ptr = idx;
            }
        }
    }
    if (local_idx >= 0)
    {
        gu_debug("local found. name[%s], seqno[%lld]",
                 group->nodes[local_idx].name,
                 (long long)gcs_node_cached(&group->nodes[local_idx]));
        return local_idx;
    }
    if (remote_idx >= 0)
    {
        gu_debug("remote found. name[%s], seqno[%lld]",
                 group->nodes[remote_idx].name,
                 (long long)gcs_node_cached(&group->nodes[remote_idx]));
        return remote_idx;
    }
    gu_debug("not found.");
    return -1;
}

static int
group_find_ist_donor (const gcs_group_t* const group,
                      int str_version,
                      int joiner_idx,
                      const char* str, int str_len,
                      gcs_seqno_t ist_seqno,
                      gcs_node_state_t status)
{
    int idx = -1;

    gcs_seqno_t conf_seqno = group->quorum.act_id;
    gcs_seqno_t lowest_cached_seqno = group_lowest_cached_seqno(group);
    if (lowest_cached_seqno == GCS_SEQNO_ILL)
    {
        gu_debug("fallback to sst. lowest_cached_seqno == GCS_SEQNO_ILL");
        return -1;
    }
    gcs_seqno_t const max_cached_range = conf_seqno - lowest_cached_seqno;
    gcs_seqno_t safety_gap = max_cached_range >> 7; /* 1.0 / 128 ~= 0.008 */
    safety_gap = safety_gap < (1 << 20) ? safety_gap : (1 << 20); /* Be sensible and don't reserve more than 1M */
    gcs_seqno_t safe_ist_seqno = lowest_cached_seqno + safety_gap;

    gu_debug("ist_seqno[%lld], lowest_cached_seqno[%lld],"
             "conf_seqno[%lld], safe_ist_seqno[%lld]",
             (long long)ist_seqno, (long long)lowest_cached_seqno,
             (long long)conf_seqno, (long long)safe_ist_seqno);

    if (ist_seqno < safe_ist_seqno) {
        // unsafe to perform ist.
        gu_debug("fallback to sst. ist_seqno < safe_ist_seqno");
        return -1;
    }

    if (str_len) {
        // find ist donor by name.
        idx = group_find_ist_donor_by_name_in_string(
            group, joiner_idx, str, str_len, ist_seqno, status);
        if (idx >= 0) return idx;
    }
    // find ist donor by status.
    idx = group_find_ist_donor_by_state(
        group, joiner_idx, ist_seqno, status);
    if (idx >= 0) return idx;
    return -1;
}

int
gcs_group_find_donor(const gcs_group_t* group,
                     int const str_version,
                     int const joiner_idx,
                     const char* const donor_string, int const donor_len,
                     const gu_uuid_t* ist_uuid, gcs_seqno_t ist_seqno)
{
    static gcs_node_state_t const min_donor_state = GCS_NODE_STATE_SYNCED;

    /* try to find ist donor first.
       if it fails, fallbacks to find sst donor*/
    int donor_idx = -1;
    if (str_version >= 2 &&
        gu_uuid_compare(&group->group_uuid, ist_uuid) == 0)
    {
        assert (ist_seqno != GCS_SEQNO_ILL);
        donor_idx = group_find_ist_donor(group,
                                         str_version,
                                         joiner_idx,
                                         donor_string, donor_len,
                                         ist_seqno,
                                         min_donor_state);
    }
    if (donor_idx < 0)
    {
        /* if donor_string is empty,
           it will fallback to find_node_by_state() */
        donor_idx = group_for_each_donor_in_string
                (group, str_version, joiner_idx,
                 donor_string, donor_len,
                 min_donor_state);
    }
    return donor_idx;
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
static int
group_select_donor (gcs_group_t* group,
                    int const str_version,
                    int const joiner_idx,
                    const char* const donor_string,
                    const gu_uuid_t* ist_uuid, gcs_seqno_t ist_seqno,
                    bool const desync)
{
    static gcs_node_state_t const min_donor_state = GCS_NODE_STATE_SYNCED;
    int  donor_idx;
    int  const donor_len = strlen(donor_string);
    bool const required_donor = (donor_len > 0);

    if (desync) { /* sender wants to become "donor" itself */
        assert(donor_len > 0);
        gcs_node_state_t const st(group->nodes[joiner_idx].status);
        if (st >= min_donor_state ||
            (st >= GCS_NODE_STATE_DONOR && group->quorum.version >= 4)) {
            donor_idx = joiner_idx;
            gcs_node_t& donor(group->nodes[donor_idx]);
            assert(donor.desync_count == 0 || group->quorum.version >= 4);
            assert(donor.desync_count == 0 || st == GCS_NODE_STATE_DONOR);
            (void)donor; // keep optimised build happy
        }
        else
            donor_idx = -EAGAIN;
    }
    else {
        donor_idx = gcs_group_find_donor(group,
                                         str_version,
                                         joiner_idx,
                                         donor_string, donor_len,
                                         ist_uuid, ist_seqno);
    }

    if (donor_idx >= 0) {
        assert(donor_idx != joiner_idx || desync);

        gcs_node_t* const joiner = &group->nodes[joiner_idx];
        gcs_node_t* const donor  = &group->nodes[donor_idx];

        donor->desync_count += 1;

        if (desync && 1 == donor->desync_count) {
            gu_info ("Member %d.%d (%s) desyncs itself from group",
                     donor_idx, donor->segment, donor->name);
        }
        else if (!desync) {
            gu_info ("Member %d.%d (%s) requested state transfer from '%s'. "
                     "Selected %d.%d (%s)(%s) as donor.",
                     joiner_idx, joiner->segment, joiner->name,
                     required_donor ? donor_string : "*any*",
                     donor_idx, donor->segment, donor->name,
                     gcs_node_state_to_str(donor->status));
        }

        // reserve donor, confirm joiner (! assignment order is significant !)
        joiner->status = GCS_NODE_STATE_JOINER;
        donor->status  = GCS_NODE_STATE_DONOR;

        if (1 == donor->desync_count) {
            /* SST or first desync */
            memcpy (donor->joiner, joiner->id, GCS_COMP_MEMB_ID_MAX_LEN+1);
            memcpy (joiner->donor, donor->id,  GCS_COMP_MEMB_ID_MAX_LEN+1);
        }
        else {
            assert(true == desync);
        }
    }
    else {
        gu_warn ("Member %d.%d (%s) requested state transfer from '%s', "
                 "but it is impossible to select State Transfer donor: %s",
                 joiner_idx, group->nodes[joiner_idx].segment,
                 group->nodes[joiner_idx].name,
                 required_donor ? donor_string : "*any*", strerror (-donor_idx));
    }

    return donor_idx;
}

/* Cleanup ignored state request */
void
gcs_group_ignore_action (gcs_group_t* group, struct gcs_act_rcvd* act)
{
    gu_debug("Ignoring action: buf: %p, len: %zd, type: %d, sender: %d, "
             "seqno: %lld", act->act.buf, act->act.buf_len, act->act.type,
             act->sender_idx, act->id);

    if (act->act.type <= GCS_ACT_STATE_REQ) {
        gcs_gcache_free (group->cache, act->act.buf);
    }

    act->act.buf     = NULL;
    act->act.buf_len = 0;
    act->act.type    = GCS_ACT_ERROR;
    act->sender_idx  = -1;
    assert (GCS_SEQNO_ILL == act->id);
}

static bool
group_desync_request (const char* const donor)
{
    return (strlen (GCS_DESYNC_REQ) == strlen(donor) &&
            !strcmp(GCS_DESYNC_REQ, donor));
}

/* NOTE: check gcs_request_state_transfer() for sender part. */
/*! Returns 0 if request is ignored, request size if it should be passed up */
int
gcs_group_handle_state_request (gcs_group_t*         group,
                                struct gcs_act_rcvd* act)
{
    // pass only to sender and to one potential donor
    const char*      donor_name     = (const char*)act->act.buf;
    size_t           donor_name_len = strlen(donor_name);
    int              donor_idx      = -1;
    int const        joiner_idx     = act->sender_idx;
    const char*      joiner_name    = group->nodes[joiner_idx].name;
    gcs_node_state_t joiner_status  = group->nodes[joiner_idx].status;
    bool const       desync         = group_desync_request (donor_name);

    gu_uuid_t ist_uuid = {{0, }};
    gcs_seqno_t ist_seqno = GCS_SEQNO_ILL;
    int str_version = 1; // actually it's 0 or 1.

    if (act->act.buf_len > (ssize_t)(donor_name_len + 2) &&
        donor_name[donor_name_len + 1] == 'V') {
        str_version = (int)donor_name[donor_name_len + 2];
    }

    if (str_version >= 2) {
        ssize_t const ist_offset(donor_name_len + 3);
        ssize_t const sst_offset
            (ist_offset + sizeof(ist_uuid) + sizeof(ist_seqno));

        if (act->act.buf_len < sst_offset)
        {
            if (group->my_idx == joiner_idx)
            {
                gu_fatal("Failed to form State Transfer Request: %zd < %zd. "
                         "Internal program error.",
                         act->act.buf_len, sst_offset);
                act->id = -ENOTRECOVERABLE;
                return act->act.buf_len;
            }
            else
            {
                gu_warn("Malformed State Transfer Request from %d.%d (%s): "
                        "%zd < %zd. Ignoring.",
                        joiner_idx, group->nodes[joiner_idx].segment,
                        joiner_name);
                gcs_group_ignore_action(group, act);
                return 0;
            }
        }

        const char* ist_buf = donor_name + ist_offset;
        memcpy(&ist_uuid, ist_buf, sizeof(ist_uuid));
        ist_seqno = gcs_seqno_gtoh(*(gcs_seqno_t*)(ist_buf + sizeof(ist_uuid)));

        // change act.buf's content to original version.
        // and it's safe to change act.buf_len
        memmove((char*)act->act.buf + donor_name_len + 1,
                (char*)act->act.buf + sst_offset,
                act->act.buf_len - sst_offset);
        act->act.buf_len -= sst_offset - donor_name_len - 1;
    }

    assert (GCS_ACT_STATE_REQ == act->act.type);

    if (joiner_status != GCS_NODE_STATE_PRIM && !desync) {

        const char* joiner_status_string = gcs_node_state_to_str(joiner_status);

        if (group->my_idx == joiner_idx) {
            if (joiner_status >= GCS_NODE_STATE_JOINED)
            {
                gu_warn ("Requesting state transfer while in %s. "
                         "Ignoring.", joiner_status_string);
                act->id = -ECANCELED;
            }
            else
            {
                /* The node can't send two STRs in a row */
                assert(joiner_status == GCS_NODE_STATE_JOINER);
                gu_fatal("Requesting state transfer while in %s. "
                         "Internal program error.", joiner_status_string);
                act->id = -ENOTRECOVERABLE;
            }
            return act->act.buf_len;
        }
        else {
            gu_warn ("Member %d.%d (%s) requested state transfer, "
                     "but its state is %s. Ignoring.",
                     joiner_idx, group->nodes[joiner_idx].segment, joiner_name,
                     joiner_status_string);
            gcs_group_ignore_action (group, act);
            return 0;
        }
    }

    donor_idx = group_select_donor(group,
                                   str_version,
                                   joiner_idx, donor_name,
                                   &ist_uuid, ist_seqno, desync);

    assert (donor_idx != joiner_idx || desync  || donor_idx < 0);
    assert (donor_idx == joiner_idx || !desync || donor_idx < 0);

    if (group->my_idx != joiner_idx && group->my_idx != donor_idx) {
        // if neither DONOR nor JOINER, ignore request
        gcs_group_ignore_action (group, act);
        return 0;
    }
    else if (group->my_idx == donor_idx) {
        act->act.buf_len -= donor_name_len + 1;
        memmove (*(void**)&act->act.buf,
                 ((char*)act->act.buf) + donor_name_len + 1,
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
        ret += sizeof(gcs_seqno_t); // cached seqno
    }

    return ret;
}

/* Creates new configuration action */
ssize_t
gcs_group_act_conf (gcs_group_t*    group,
                    struct gcs_act* act,
                    int*            gcs_proto_ver)
{
    // if (*gcs_proto_ver < group->quorum.gcs_proto_ver)
    //     *gcs_proto_ver = group->quorum.gcs_proto_ver; // only go up, see #482
    // else if (group->quorum.gcs_proto_ver >= 0 &&
    //          group->quorum.gcs_proto_ver < *gcs_proto_ver) {
    //     gu_warn ("Refusing GCS protocol version downgrade from %d to %d",
    //              *gcs_proto_ver, group->quorum.gcs_proto_ver);
    // }

    // actually we allow gcs protocol version downgrade.
    // because if message version is inconsistent with gcs protocol version
    // gcs requires resending message with correct gcs protocol version.
    *gcs_proto_ver = group->quorum.gcs_proto_ver;

    ssize_t conf_size = sizeof(gcs_act_conf_t) + group_memb_record_size(group);
    gcs_act_conf_t* conf = static_cast<gcs_act_conf_t*>(malloc (conf_size));

    if (conf) {
        long idx;

        conf->seqno          = group->act_id_;
        conf->conf_id        = group->conf_id;
        conf->memb_num       = group->num;
        conf->my_idx         = group->my_idx;
        conf->repl_proto_ver = group->quorum.repl_proto_ver;
        conf->appl_proto_ver = group->quorum.appl_proto_ver;

        memcpy (conf->uuid, &group->group_uuid, sizeof (gu_uuid_t));

        if (group->num) {
            assert (conf->my_idx >= 0);

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
                gcs_seqno_t cached = gcs_node_cached(&group->nodes[idx]);
                memcpy(ptr, &cached, sizeof(cached));
                ptr += sizeof(cached);
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
group_get_node_state (const gcs_group_t* const group, long const node_idx)
{
    const gcs_node_t* const node = &group->nodes[node_idx];

    uint8_t flags = 0;

    if (0 == node_idx)            flags |= GCS_STATE_FREP;
    if (node->count_last_applied) flags |= GCS_STATE_FCLA;
    if (node->bootstrap)          flags |= GCS_STATE_FBOOTSTRAP;
#ifdef GCS_FOR_GARB
    flags |= GCS_STATE_ARBITRATOR;

    int64_t const cached = GCS_SEQNO_ILL;
#else
    int64_t const cached = /* group->cache check is needed for unit tests */
        group->cache ? gcache_seqno_min(group->cache) : GCS_SEQNO_ILL;
#endif /* GCS_FOR_GARB */

    return gcs_state_msg_create (
        &group->state_uuid,
        &group->group_uuid,
        &group->prim_uuid,
        group->prim_seqno,
        group->act_id_,
        cached,
        group->prim_num,
        group->prim_state,
        node->status,
        node->name,
        node->inc_addr,
        node->gcs_proto_ver,
        node->repl_proto_ver,
        node->appl_proto_ver,
        group->prim_gcs_ver,
        group->prim_repl_ver,
        group->prim_appl_ver,
        node->desync_count,
        flags
        );
}

/*! Returns state message object for this node */
gcs_state_msg_t*
gcs_group_get_state (const gcs_group_t* group)
{
    return group_get_node_state (group, group->my_idx);
}

void
gcs_group_get_status (const gcs_group_t* group, gu::Status& status)
{
    int desync_count; // make sure it is not initialized

    if (gu_likely(group->my_idx >= 0))
    {
        const gcs_node_t& this_node(group->nodes[group->my_idx]);

        desync_count = this_node.desync_count;
    }
    else
    {
        desync_count = 0;
    }

    status.insert("desync_count", gu::to_string(desync_count));
}


