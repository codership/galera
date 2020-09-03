/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcs_group.hpp"
#include "gcs_gcache.hpp"
#include "gcs_priv.hpp"
#include "gcs_code_msg.hpp"

#include <gu_logger.hpp>
#include <gu_macros.hpp>
#include <gu_unordered.hpp>

#include <errno.h>

#include <limits>

std::string const GCS_VOTE_POLICY_KEY("gcs.vote_policy");
uint8_t     const GCS_VOTE_POLICY_DEFAULT(0);

void gcs_group_register(gu::Config* cnf)
{
    cnf->add(GCS_VOTE_POLICY_KEY);
}

const char* gcs_group_state_str[GCS_GROUP_STATE_MAX] =
{
    "NON_PRIMARY",
    "WAIT_STATE_UUID",
    "WAIT_STATE_MSG",
    "PRIMARY"
};


uint8_t gcs_group_conf_to_vote_policy(gu::Config& cnf)
{
    int64_t i(cnf.get(GCS_VOTE_POLICY_KEY, int64_t(GCS_VOTE_POLICY_DEFAULT)));

    if (i < 0 || i >= std::numeric_limits<uint8_t>::max())
    {
        log_warn << "Bogus '" << GCS_VOTE_POLICY_KEY << "' from config: " << i
                 << ". Reverting to default."; // or throw?
        return GCS_VOTE_POLICY_DEFAULT;
    }

    return i;
}

int
gcs_group_init (gcs_group_t* group, gu::Config* const cnf, gcache_t* const cache,
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
    group->num          = 0;
    group->my_idx       = -1;
    group->my_name      = strdup(node_name ? node_name : NODE_NO_NAME);
    group->my_address   = strdup(inc_addr  ? inc_addr  : NODE_NO_ADDR);
    group->state        = GCS_GROUP_NON_PRIMARY;
    group->last_applied = group->act_id_;
    group->last_node    = -1;
    group->vote_request_seqno = GCS_NO_VOTE_SEQNO;
    group->vote_result  = (VoteResult){ GCS_NO_VOTE_SEQNO, 0 };
    group->vote_history = new VoteHistory;
    group->vote_policy  = gcs_group_conf_to_vote_policy(*cnf);
    group->frag_reset   = true; // just in case
    group->nodes        = NULL;
    group->prim_uuid    = GU_UUID_NIL;
    group->prim_seqno   = GCS_SEQNO_ILL;
    group->prim_num     = 0;
    group->prim_state   = GCS_NODE_STATE_NON_PRIM;
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
gcs_group_init_history (gcs_group_t*    group,
                        const gu::GTID& gtid)
{
    bool const negative_seqno(gtid.seqno() < 0);
    bool const nil_uuid(gtid.uuid() == GU_UUID_NIL);

    if (negative_seqno && !nil_uuid) {
        log_error << "Non-nil history UUID with negative seqno makes no sense: "
                  << gtid;
        return -EINVAL;
    }
    else if (!negative_seqno && nil_uuid) {
        log_error <<"Non-negative state seqno requires non-nil history UUID: "
                  << gtid;
        return -EINVAL;
    }

    group->act_id_    = gtid.seqno();
    group->last_applied = group->act_id_;
    group->group_uuid = gtid.uuid()();
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
            assert(ret[i].last_applied == GCS_SEQNO_NIL);
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
    delete group->vote_history;
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

/*! @return false
 *  if the node is arbitrator and must not be counted in commit cut */
static inline bool
group_count_arbitrator(const gcs_group_t& group, const gcs_node_t& node)
{
    return (!(group.quorum.gcs_proto_ver > 0 && node.arbitrator));
}

/*! @return true if the node should be counted in commit cut calculations */
static inline bool
group_count_last_applied(const gcs_group_t& group, const gcs_node_t& node)
{
    return (node.count_last_applied && group_count_arbitrator(group, node));
}

/* Find node with the smallest last_applied */
static inline void
group_redo_last_applied (gcs_group_t* group)
{
    gu_seqno_t last_applied = GU_LLONG_MAX;
    int        last_node    = -1;
    int        n;

    for (n = 0; n < group->num; n++) {
        const gcs_node_t* const node = &group->nodes[n];
        gcs_seqno_t const seqno = node->last_applied;

        assert( 0  < group->last_applied_proto_ver ||
               -1 == group->last_applied_proto_ver /* for unit tests */);

        log_debug << "last_last_applied[" << n << "]: "
                  << node->id << ", " << node->last_applied << ", "
                  << (group_count_last_applied(*group, *node) ? "yes" : "no");

        /* NOTE: It is crucial for consistency that last_applied algorithm
         *       is absolutely identical on all nodes. Therefore for the
         *       generality sake and future compatibility we have to assume
         *       non-blocking donor.
         *       GCS_BLOCKING_DONOR should never be defined unless in some
         *       very custom builds. Commenting it out for safety sake. */
#ifndef GCS_BLOCKING_DONOR
        if (group_count_last_applied(*group, *node)
#else
        if ((GCS_NODE_STATE_SYNCED == node->status) /* ignore donor */
#endif
            && (seqno <= last_applied)) {
#ifndef NDEBUG
            if (seqno > 0 && seqno < group->last_applied)
            {
                log_info << "Node:\n" << *node
                         << "\nattempts to set last_applied to " << seqno
                         << " below the current " << group->last_applied;
            }
#endif /* NDEBUG */
            if (seqno >= group->last_applied || group->quorum.gcs_proto_ver < 2)
            {
                last_applied = seqno;
                last_node    = n;
            }
            else if (seqno < group->last_applied)
            {
                if (0 != seqno)
                {
                    log_debug << "Last applied: " << seqno
                              << " at node " << node->id
                              << " is less than group last applied: "
                              << group->last_applied;
                    /* This is a possible situation since we allow for
                     * the non-determinism in the last applied reporting.
                     * Even a synced node can report a slightly lower number
                     * depending on when it decides to report. */
                }
                // the node has not yet reported its last applied
            }
        }
        // extra diagnostic, ignore
        //else if (!count) { gu_warn("not counting %d", n); }
    }

    if (gu_likely (last_node >= 0)) {
        assert(last_applied >= group->last_applied ||
               group->quorum.gcs_proto_ver < 2);
        group->last_applied = last_applied;
        group->last_node    = last_node;
    }

    log_debug << "final last_applied on " << group->nodes[group->my_idx].name
              << "): " << group->last_applied;
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
    assert(quorum->version >= 2);

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

            if (quorum->gcs_proto_ver >= 2) // see below for older version
            {
                assert(quorum->last_applied >= 0);
                group->last_applied = quorum->last_applied;
            }
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

        if (quorum->gcs_proto_ver < 2) // see above for newer version
        {
            group_redo_last_applied(group);
        }
        // votes will be recounted on CC action creation
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
             "\n\tvote policy= %d,"
             "\n\tgroup UUID = " GU_UUID_FORMAT,
             quorum->version,
             quorum->primary ? "PRIMARY" : "NON-PRIMARY",
             quorum->conf_id,
             group->prim_num, group->num,
             quorum->act_id,
             group->last_applied,
             quorum->gcs_proto_ver, quorum->repl_proto_ver,
             quorum->appl_proto_ver,
             int(quorum->vote_policy),
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
        gu_info ("New SELF-LEAVE.");
        assert (0 == new_nodes_num);
        assert (!prim_comp);
    }

    bool my_bootstrap(bootstrap);

    if (prim_comp) {
        /* Got PRIMARY COMPONENT - Hooray! */
        assert (new_my_idx >= 0);
        if (group->state == GCS_GROUP_PRIMARY) {
            /* we come from previous primary configuration, relax */
            assert(group->my_idx >= 0);
            my_bootstrap = group->nodes[group->my_idx].bootstrap;
        }
        else if (bootstrap && gu_uuid_compare(&group->group_uuid,
                                              &GU_UUID_NIL))
        {
            /* Is there need to initialize something else in this case? */
            my_bootstrap = true;
        }
        else {
            const bool first_component =
#ifndef GCS_CORE_TESTING
                (0 == group->num) || bootstrap;
#else
                (0 == group->num);
#endif
            if (1 == new_nodes_num && first_component) {
                /* bootstrap new configuration */
                assert (GCS_GROUP_NON_PRIMARY == group->state);
                assert ((0 == group->num && -1 == group->my_idx) ||
                        /* if first comp was non prim due to group expulsion */
                        (1 == group->num &&  0 == group->my_idx));

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

                group->last_applied = group->act_id_;
                assert(group->last_applied >= 0);

                new_nodes[0].status = GCS_NODE_STATE_JOINED;
                new_nodes[0].last_applied = group->last_applied;
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

    assert(group->num > 0 || group->my_idx < 0);
    assert(group->my_idx >= 0 || group->num == 0);

    if (group->my_idx >= 0) group->nodes[group->my_idx].bootstrap = my_bootstrap;

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

        if (group->quorum.gcs_proto_ver < 2)
        {
            // commit cut recomputation should happen only after state exchange
            group_redo_last_applied (group);
        }
    }

    return group->state;
}

gcs_group_state_t
gcs_group_handle_uuid_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    assert (msg->size == sizeof(gu_uuid_t));

    if (GCS_GROUP_WAIT_STATE_UUID == group->state &&
        0 == msg->sender_idx /* check that it is from the representative */) {
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

gcs_group_state_t
gcs_group_handle_state_msg (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    if (GCS_GROUP_WAIT_STATE_MSG == group->state) {

        gcs_state_msg_t* state = gcs_state_msg_read (msg->buf, msg->size);

        if (state) {
            char state_str[1024];
            gcs_state_msg_snprintf(state_str, sizeof(state_str), state);

            const gu_uuid_t* state_uuid = gcs_state_msg_uuid (state);

            if (!gu_uuid_compare(&group->state_uuid, state_uuid)) {

                gu_info ("STATE EXCHANGE: got state msg: " GU_UUID_FORMAT
                         " from %d (%s)", GU_UUID_ARGS(state_uuid),
                         msg->sender_idx, gcs_state_msg_name(state));
                gu_debug("%s", state_str);

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
                gu_debug ("%s", state_str);

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

/* this is a helper function that takes care of preper interpretation of the
 * code message depending on the protocol version used.
 * @return 0 - success, -EMSGSIZE - wrong message size, -EINVAL - wrong group */
int
group_unserialize_code_msg(gcs_group_t* group, const gcs_recv_msg_t* msg,
                           gu::GTID& gtid, int64_t& code)
{
    if (gu_likely(group->gcs_proto_ver >= 1 &&
                  msg->size >= gcs::core::CodeMsg::serial_size()))
    {
        const gcs::core::CodeMsg* const cm
            (static_cast<const gcs::core::CodeMsg*>(msg->buf));

        cm->unserialize(gtid, code);

        if (gu_unlikely(gtid.uuid() != group->group_uuid))
        {
            log_info << gcs_msg_type_string[msg->type] << " message " << *cm
                     << " from another group (" << gtid.uuid()
                     << "). Dropping message.";
            return -EINVAL;
        }
    }
    else // gcs_seqno_t
    {
        if (gu_likely(msg->size == sizeof(gcs_seqno_t)))
        {
            gtid.set(gu::gtoh(*(static_cast<const gcs_seqno_t*>(msg->buf))));
            code = 0;
        }
        else
        {
            log_warn << "Bogus size for " << gcs_msg_type_string[msg->type]
                     << " message: " << msg->size << " bytes. Dropping message.";
            return -EMSGSIZE;
        }
    }

    return 0;
}

/*! Returns new last applied value if it has changes, 0 otherwise */
gcs_seqno_t
gcs_group_handle_last_msg (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    assert (GCS_MSG_LAST == msg->type);

    gu::GTID gtid;
    int64_t  code;

    if (gu_unlikely(group_unserialize_code_msg(group, msg, gtid,code))) return 0;

    if (gu_unlikely(0 != code))
    {
        log_warn << "Bogus " << gcs_msg_type_string[msg->type]
                 << " message code: " << code <<". Ignored.";
        assert(0);
        return 0;
    }

    // This assert is too restrictive. It requires application to send
    // last applied messages while holding TO, otherwise there's a race
    // between threads.
    // assert (seqno >= group->last_applied);

    gcs_node_set_last_applied (&group->nodes[msg->sender_idx], gtid.seqno());
    assert(group->nodes[msg->sender_idx].last_applied >= 0);

    if (msg->sender_idx == group->last_node   &&
        gtid.seqno()    >  group->last_applied) {
        /* node that was responsible for the last value, has changed it.
         * need to recompute it */
        gcs_seqno_t old_val = group->last_applied;

        group_redo_last_applied (group);

        if (old_val < group->last_applied) {
            gu_debug ("New COMMIT CUT %lld on %d after %lld from %d",
                      (long long)group->last_applied, group->my_idx,
                      (long long)gtid.seqno(), msg->sender_idx);
            return group->last_applied;
        }
    }

    return 0;
}

/*! @return true if the node's vote must be counted */
static inline bool
group_count_votes(const gcs_node_t& node)
{
    return (node.count_last_applied && !node.arbitrator);
}

/* true if last vote was updated, false if not */
static bool
group_recount_votes (gcs_group_t& group)
{
    typedef std::pair<uint64_t, int> VoteEntry;
    typedef std::map<uint64_t, int>  VoteCounts; //we want it consistently sorted
    typedef VoteCounts::const_iterator VoteCountsIt;

    bool voting(false);
    gcs_seqno_t voting_seqno(group.act_id_);

    for (int n(0); n < group.num; ++n)
    {
        const gcs_node_t& node(group.nodes[n]);

        if (group_count_votes(node) &&
            node.vote_seqno > group.vote_result.seqno)
        {
            voting = true;
            if (node.vote_seqno < voting_seqno) voting_seqno = node.vote_seqno;
        }
    }

    if (!voting) return false; /* this can happen on config. change */

    VoteCounts vc;
    int n_votes(0);
    int voters(0);

    for (int n(0); n < group.num; ++n)
    {
        gcs_node_t& node(group.nodes[n]);

        if (group_count_votes(node) || node.last_applied >= voting_seqno)
        {
            ++voters;

            if (node.vote_seqno   >= voting_seqno ||
                node.last_applied >= voting_seqno)
            {
                ++n_votes;

                /* If a node has voted on seqno > voting_seqno or
                 * reported last appied on a seqno >= voting_seqno,
                 * then its vote for the voting_seqno is 0 (success) */
                uint64_t const vote
                    (node.vote_seqno == voting_seqno ? node.vote_res : 0);

                vc.insert(VoteEntry(vote, 0)).first->second++;
            }
        }
        else
        {
            log_debug << "Excluding node from voters: " << node;
        }
    }

    assert(n_votes > 0);

    gu::GTID const vote_gtid(group.group_uuid, voting_seqno);
    std::ostringstream diag;
    diag << "Votes over " << vote_gtid << ":\n";

    int max_count(0);
    int second_max(0);
    int zero_count(0);
    uint64_t max_vote(0);
#ifndef NDEBUG
    int counts(0);
#endif
    for (VoteCountsIt it(vc.begin()); it != vc.end(); ++it)
    {
        assert(it->second > 0);

        if (0 == it->first) zero_count = it->second;

        if (it->second >= max_count)
        {
            second_max = max_count;
            max_vote   = it->first;
            max_count  = it->second;
        }
#ifndef NDEBUG
        counts += it->second;
#endif
        diag << "   " << gu::PrintBase<>(it->first) << ": "
             << std::setfill(' ') << std::setw(3) << it->second << '/'
             << std::setw(0) << voters << "\n";
    }
    assert(counts == n_votes);
    assert(zero_count <= max_count);

    int const missing(voters - n_votes);

    uint64_t win_vote;
    if (group.quorum.vote_policy > 0 &&
        zero_count >= int(group.quorum.vote_policy))
    {
        win_vote = 0;
    }
    else if ((0 == group.quorum.vote_policy ||
              (zero_count + missing < int(group.quorum.vote_policy))) &&
             /* what is happening here: for zero vote to win it must be >=
              * than any other vote. Which requires any other vote to be
              * STRICTLY > in case zero count is the second runner up. Yet
              * it is sufficient to be >= otherwise. */
             (zero_count >= second_max + missing /* zero_count == max_count */||
              max_count  >= second_max + missing + (zero_count == second_max)))
    {
        /* even if received, missing votes won't win over current max */
        win_vote = (zero_count >= max_count ? 0 : max_vote);
    }
    else
    {
        diag << "Waiting for more votes.";
        log_info << diag.str();
        assert(missing > 0);
        return false;;
    }

    diag << "Winner: " << gu::PrintBase<>(win_vote);
    log_info << diag.str();

    group.vote_result.seqno = voting_seqno;
    group.vote_result.res   = win_vote;

    const gcs_node_t& this_node(group.nodes[group.my_idx]);
    if (this_node.vote_seqno < voting_seqno)
    {
        // record voting result in the history for later
        std::pair<gu::GTID,int64_t> const val(vote_gtid, win_vote);
        std::pair<VoteHistory::iterator, bool> const res
                    (group.vote_history->insert(val));
        if (false == res.second)
        {
            assert(0);
            res.first->second = group.vote_result.res;
        }
    }

    return true;
}

VoteResult
gcs_group_handle_vote_msg (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    assert (GCS_MSG_VOTE == msg->type);

    gu::GTID gtid;
    int64_t  code;

    gcs_node_t& sender(group->nodes[msg->sender_idx]);

    if (gu_unlikely(group_unserialize_code_msg(group, msg, gtid, code)))
    {
        log_warn << "Failed to deserialize vote msg from " << msg->sender_idx
                 << " (" << sender.name << ")";
        VoteResult const ret = { GCS_NO_VOTE_SEQNO, 0 };
        return ret;
    }

    if (gtid.uuid() == group->group_uuid &&
        gtid.seqno() > group->vote_result.seqno)
    {
        const char* const data
            (gcs::core::CodeMsg::serial_size() < msg->size ?
             (static_cast<char*>(msg->buf) + gcs::core::CodeMsg::serial_size()) :
             NULL);

        /* voting on this seqno has not completed yet */
        log_info << "Member " << msg->sender_idx << '(' << sender.name << ") "
                 << (code ? "initiates" : "responds to") << " vote on "
                 << gtid << ',' << gu::PrintBase<>(code) << ": "
                 << (code ? (data ? data : "(null)") : "Success");

        gcs_node_set_vote (&sender, gtid.seqno(), code);

        if (group_recount_votes(*group))
        {
            /* What if group->vote_result.seqno < gtid.seqno()?
             * - that means that there is inconsistency between the sender and
             * the member who initiated voting on vote_result.seqno. This in turn
             * means that there will be a configuration change that will trigger
             * another votes recount, and then another configuration change
             * - until we reach gtid.senqo() */
            if (group->nodes[group->my_idx].vote_seqno >=
                group->vote_result.seqno)
            {
                return group->vote_result;
            }
        }
        else if (gtid.seqno() > group->vote_request_seqno)
        {
            group->vote_request_seqno = gtid.seqno();
            if (msg->sender_idx != group->my_idx)
            {
                VoteResult const ret = { gtid.seqno(), GCS_VOTE_REQUEST };
                return ret;
            }
        }
    }
    else if (msg->sender_idx == group->my_idx)
    {
        std::ostringstream msg;
        msg << "Recovering vote result from history: " << gtid;

        int64_t result(0);
        VoteHistory::iterator it(group->vote_history->find(gtid));
        if (group->vote_history->end() != it)
        {
            result = it->second;
            group->vote_history->erase(it);
            msg << ',' << gu::PrintBase<>(result);
        }
        else
        {
            msg << ": not found";
            assert(code < 0);
            /* by default result is 0, which means success/no voting happened,
             * and this node is the only inconsistent one. */
        }

        log_info << msg.str();

        VoteResult const ret = { gtid.seqno(), result };
        return ret; // this should wake up the thread that voted
    }
    else if (gtid.seqno() > group->vote_result.seqno)
    {
        /* outdated vote from another member, ignore */
        log_info << "Outdated vote "
                 << gu::PrintBase<>(code) << " for " << gtid;
        log_info << "(last group vote was on: "
                 << gu::GTID(group->group_uuid, group->vote_result.seqno) << ','
                 << gu::PrintBase<>(group->vote_result.res) << ')';
    }

    VoteResult const ret = { GCS_NO_VOTE_SEQNO, 0 };  // no action required
    return ret;
}

/*! return true if this node is the sender to notify the calling thread of
 * success */
int
gcs_group_handle_join_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    int const   sender_idx = msg->sender_idx;
    gcs_node_t* sender    = &group->nodes[sender_idx];

    assert (GCS_MSG_JOIN == msg->type);

    gu::GTID gtid;
    int64_t  code;

    if (gu_unlikely(group_unserialize_code_msg(group, msg, gtid,code))) return 0;

    if (GCS_NODE_STATE_DONOR  == sender->status ||
        GCS_NODE_STATE_JOINER == sender->status) {
        int j;
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
                if (code >= 0) {
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

        if (code < 0) {
            gu_warn ("%d.%d (%s): State transfer %s %d.%d (%s) failed: %d (%s)",
                     sender_idx, sender->segment, sender->name, st_dir,
                     peer_idx, peer ? peer->segment : -1, peer_name,
                     (int)code, strerror((int)-code));

            if (from_donor && peer_idx == group->my_idx &&
                GCS_NODE_STATE_JOINER == group->nodes[peer_idx].status) {
                // this node will be waiting for SST forever. If it has only
                // one recv thread there is no (generic) way to wake it up.
                gu_fatal ("Will never receive state. Need to abort.");
                // return to core to shutdown the backend before aborting
                return -ENOTRECOVERABLE;
            }

            assert(group->quorum.version >= 2);
            if (group->quorum.version < 2 && !from_donor && // #591
                sender_idx == group->my_idx) {
                // remove after quorum v1 is phased out
                gu_fatal ("Failed to receive state. Need to abort.");
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

/* @return true if this node is sender, false otherwise */
int
gcs_group_handle_sync_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg)
{
    int const   sender_idx = msg->sender_idx;
    gcs_node_t* sender     = &group->nodes[sender_idx];

    assert (GCS_MSG_SYNC == msg->type);

    gu::GTID gtid;
    int64_t  code;

    if (gu_unlikely(group_unserialize_code_msg(group, msg, gtid,code))) return 0;

    if (GCS_NODE_STATE_JOINED == sender->status ||
        /* #454 - at this layer we jump directly from DONOR to SYNCED */
        (0 == group->last_applied_proto_ver &&
         GCS_NODE_STATE_DONOR == sender->status)) {

        sender->status = GCS_NODE_STATE_SYNCED;
        sender->count_last_applied = group_count_arbitrator(*group, *sender);

        group_redo_last_applied (group); //from now on this node must be counted

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
group_find_node_by_state (const gcs_group_t* const group,
                          int                const joiner_idx,
                          gcs_node_state_t   const status)
{
    gcs_segment_t const segment = group->nodes[joiner_idx].segment;
    int  idx;
    int  donor = -1;
    bool hnss = false; /* have nodes in the same segment */

    for (idx = 0; idx < group->num; idx++) {

        if (joiner_idx == idx) continue; /* skip joiner */

        gcs_node_t* node = &group->nodes[idx];

        if (node->status >= status && group_node_is_stateful (group, node))
        {
            donor = idx; /* potential donor */
        }

        if (segment == node->segment) {
            if (donor == idx) return donor; /* found suitable donor in the
                                             * same segment */
            if (node->status >= GCS_NODE_STATE_JOINER) hnss = true;
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
                     const gu::GTID& ist_gtid)
{
    static gcs_node_state_t const min_donor_state = GCS_NODE_STATE_SYNCED;

    /* try to find ist donor first.
       if it fails, fallbacks to find sst donor*/
    int donor_idx = -1;

    if (str_version >= 2 &&
        ist_gtid.uuid() == group->group_uuid &&
        ist_gtid.seqno() != GCS_SEQNO_ILL)
    {
        // FIXME: check if disabling the assertion and allowing ist_seqno to
        // equal to GCS_SEQNO_ILL requires protocol upgrade
        // assert(ist_seqno != GCS_SEQNO_ILL);

        donor_idx = group_find_ist_donor(group,
                                         str_version,
                                         joiner_idx,
                                         donor_string, donor_len,
                                         ist_gtid.seqno(),
                                         min_donor_state);
    }

    if (donor_idx < 0)
    {
        /* if donor_string is empty, it will fallback to find_node_by_state() */
        donor_idx = group_for_each_donor_in_string (group, str_version,
                                                    joiner_idx, donor_string,
                                                    donor_len, min_donor_state);
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
                    const gu::GTID& ist_gtid,
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
        donor_idx = gcs_group_find_donor(group, str_version, joiner_idx,
                                         donor_string, donor_len, ist_gtid);
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

    if (act->act.type <= GCS_ACT_CCHANGE) {
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
    const char* const donor_name    = (const char*)act->act.buf;
    size_t const     donor_name_len = strlen(donor_name) + 1;
    int              donor_idx      = -1;
    int const        joiner_idx     = act->sender_idx;
    const char*      joiner_name    = group->nodes[joiner_idx].name;
    gcs_node_state_t joiner_status  = group->nodes[joiner_idx].status;
    bool const       desync         = group_desync_request (donor_name);

    gu::GTID ist_gtid;
    int str_version = 1; // actually it's 0 or 1.

    if (act->act.buf_len > (ssize_t)donor_name_len &&
        donor_name[donor_name_len + 0] == 'V') {
        str_version = (int)donor_name[donor_name_len + 1];
    }

    if (str_version >= 2) {
        size_t offset(donor_name_len + 2);

        try
        {
            offset = ist_gtid.unserialize(act->act.buf, act->act.buf_len,offset);
        }
        catch (gu::Exception& e) {
            log_warn << "Malformed state transfer request: " << e.what()
                     << " Ignoring";
            gcs_group_ignore_action(group, act);
            return 0;
        }

        // change act.buf's content to original version.
        // and it's safe to change act.buf_len
        ::memmove((char*)act->act.buf + donor_name_len,
                  (char*)act->act.buf + offset,
                  act->act.buf_len - offset);
        act->act.buf_len -= offset - donor_name_len;
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

    donor_idx = group_select_donor(group, str_version, joiner_idx, donor_name,
                                   ist_gtid, desync);

    assert (donor_idx != joiner_idx || desync  || donor_idx < 0);
    assert (donor_idx == joiner_idx || !desync || donor_idx < 0);

    if (group->my_idx != joiner_idx && group->my_idx != donor_idx) {
        // if neither DONOR nor JOINER, ignore request
        gcs_group_ignore_action (group, act);
        return 0;
    }
    else if (group->my_idx == donor_idx) {
        act->act.buf_len -= donor_name_len;
        memmove (*(void**)&act->act.buf,
                 ((char*)act->act.buf) + donor_name_len,
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

/* Creates new configuration action */
ssize_t
gcs_group_act_conf (gcs_group_t*         group,
                    struct gcs_act_rcvd* rcvd,
                    int*                 gcs_proto_ver)
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

    struct gcs_act_cchange conf;

    if (GCS_GROUP_PRIMARY == group->state) {
        if (group->quorum.gcs_proto_ver >= 1)
        {
            ++group->act_id_;

            if (group_recount_votes(*group))
            {
                conf.vote_seqno = group->vote_result.seqno;
                conf.vote_res   = group->vote_result.res;
            }
        }
    }
    else {
        assert(GCS_GROUP_NON_PRIMARY == group->state);
    }

    conf.seqno          = group->act_id_;
    conf.conf_id        = group->conf_id;
    conf.repl_proto_ver = group->quorum.repl_proto_ver;
    conf.appl_proto_ver = group->quorum.appl_proto_ver;

    memcpy (conf.uuid.data, &group->group_uuid, sizeof (gu_uuid_t));

    if (group->num) {
        assert (group->my_idx >= 0);

        for (int idx = 0; idx < group->num; ++idx)
        {
            gcs_act_cchange::member m;

            gu_uuid_scan(group->nodes[idx].id, strlen(group->nodes[idx].id),
                         &m.uuid_);
            m.name_     = group->nodes[idx].name;
            m.incoming_ = group->nodes[idx].inc_addr;
            m.cached_   = gcs_node_cached(&group->nodes[idx]);
            m.state_    = group->nodes[idx].status;

            conf.memb.push_back(m);
        }
    }
    else {
        // self leave message
        assert (conf.conf_id < 0);
        assert (-1 == group->my_idx);
    }

    void* tmp;
    rcvd->act.buf_len = conf.write(&tmp); // throws when fails
#ifndef GCS_FOR_GARB
    /* copy CC event to gcache for IST */
    rcvd->act.buf = gcache_malloc(group->cache, rcvd->act.buf_len);
    if (rcvd->act.buf)
    {
        memcpy(const_cast<void*>(rcvd->act.buf), tmp, rcvd->act.buf_len);
        rcvd->id = group->my_idx; // passing own index in seqno_g
    }
    else
    {
        rcvd->act.buf_len = -ENOMEM;
        rcvd->id          = -ENOMEM;
    }
    free(tmp);
#else
    rcvd->act.buf = tmp;
    rcvd->id = group->my_idx;
#endif /* GCS_FOR_GARB */

    rcvd->act.type = GCS_ACT_CCHANGE;

    return rcvd->act.buf_len;
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
        group->last_applied, // should be the same global property as act_id_
        node->vote_seqno,
        node->vote_res,
        group->vote_policy,
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

int
gcs_group_param_set(gcs_group_t& group,
                    const std::string& key, const std::string& val)
{
    if (GCS_VOTE_POLICY_KEY == key)
    {
        gu_throw_error(ENOTSUP) << "Setting '" << key << "' in runtime may "
            "have unintended consequences and is currently not supported. "
            "Cluster voting policy should be decided on before starting the "
            "cluster.";
    }

    return 1;
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

