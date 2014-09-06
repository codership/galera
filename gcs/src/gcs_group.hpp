/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*
 * This header defines node specific context we need to maintain
 */

#ifndef _gcs_group_h_
#define _gcs_group_h_

#include <stdbool.h>

#include <galerautils.h>

#include "gcs_node.hpp"
#include "gcs_recv_msg.hpp"
#include "gcs_seqno.hpp"
#include "gcs_state_msg.hpp"

#include <gcache.h>

typedef enum gcs_group_state
{
    GCS_GROUP_NON_PRIMARY,
    GCS_GROUP_WAIT_STATE_UUID,
    GCS_GROUP_WAIT_STATE_MSG,
    GCS_GROUP_PRIMARY,
    GCS_GROUP_STATE_MAX
}
gcs_group_state_t;

extern const char* gcs_group_state_str[];

typedef struct gcs_group
{
    gcache_t*     cache;
    gcs_seqno_t   act_id;       // current(last) action seqno
    gcs_seqno_t   conf_id;      // current configuration seqno
    gu_uuid_t     state_uuid;   // state exchange id
    gu_uuid_t     group_uuid;   // group UUID
    long          num;          // number of nodes
    long          my_idx;       // my index in the group
    const char*   my_name;
    const char*   my_address;
    gcs_group_state_t state;    // group state: PRIMARY | NON_PRIMARY
    gcs_seqno_t   last_applied; // last_applied action group-wide
    long          last_node;    // node that reported last_applied
    bool          frag_reset;   // indicate that fragmentation was reset
    gcs_node_t*   nodes;        // array of node contexts

    /* values from the last primary component */
    gu_uuid_t        prim_uuid;
    gu_seqno_t       prim_seqno;
    long             prim_num;
    gcs_node_state_t prim_state;

    /* max supported protocols */
    gcs_proto_t const gcs_proto_ver;
    int         const repl_proto_ver;
    int         const appl_proto_ver;

    gcs_state_quorum_t quorum;
    int last_applied_proto_ver;

    gcs_group() : gcs_proto_ver(0), repl_proto_ver(0), appl_proto_ver(0) { }

}
gcs_group_t;

/*!
 * Initialize group at startup
 */
extern int
gcs_group_init (gcs_group_t* group,
                gcache_t*    cache,
                const char*  node_name, ///< can be null
                const char*  inc_addr,  ///< can be null
                gcs_proto_t  gcs_proto_ver,
                int          repl_proto_ver,
                int          appl_proto_ver);

/*!
 * Initialize group action history parameters. See gcs.h
 */
extern int
gcs_group_init_history (gcs_group_t*     group,
                        gcs_seqno_t      seqno,
                        const gu_uuid_t* uuid);

/*!
 * Free group resources
 */
extern void
gcs_group_free (gcs_group_t* group);

/*! Forget the action if it is not to be delivered */
extern void
gcs_group_ignore_action (gcs_group_t* group, struct gcs_act_rcvd* rcvd);

/*!
 * Handles component message - installs new membership,
 * cleans old one.
 *
 * @return
 *        group state in case of success or
 *        negative error code.
 */
extern gcs_group_state_t
gcs_group_handle_comp_msg  (gcs_group_t* group, const gcs_comp_msg_t* msg);

extern gcs_group_state_t
gcs_group_handle_uuid_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg);

extern gcs_group_state_t
gcs_group_handle_state_msg (gcs_group_t* group, const gcs_recv_msg_t* msg);

extern gcs_seqno_t
gcs_group_handle_last_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg);

/*! @return 0 for success, 1 for (success && i_am_sender)
 * or negative error code */
extern int
gcs_group_handle_join_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg);

/*! @return 0 for success, 1 for (success && i_am_sender)
 * or negative error code */
extern int
gcs_group_handle_sync_msg  (gcs_group_t* group, const gcs_recv_msg_t* msg);

/*! @return 0 if request is ignored, request size if it should be passed up */
extern int
gcs_group_handle_state_request (gcs_group_t*         group,
                                struct gcs_act_rcvd* act);
/*!
 * Handles action message. Is called often - therefore, inlined
 *
 * @return negative - error code, 0 - continue, positive - complete action
 */
static inline ssize_t
gcs_group_handle_act_msg (gcs_group_t*          const group,
                          const gcs_act_frag_t* const frg,
                          const gcs_recv_msg_t* const msg,
                          struct gcs_act_rcvd*  const rcvd,
                          bool not_commonly_supported_version = false)
{
    long const sender_idx = msg->sender_idx;
    bool const local      = (sender_idx == group->my_idx);
    ssize_t ret;

    assert (GCS_MSG_ACTION == msg->type);
    assert (sender_idx < group->num);
    assert (frg->act_id > 0);
    assert (frg->act_size > 0);

    // clear reset flag if set by own first fragment after reset flag was set
    group->frag_reset = (group->frag_reset && (!local || (0 != frg->frag_no)));

    ret = gcs_node_handle_act_frag (&group->nodes[sender_idx], frg, &rcvd->act,
                                    local);

    if (ret > 0) {

        assert (ret == rcvd->act.buf_len);

        rcvd->act.type = frg->act_type;

        if (gu_likely(GCS_ACT_TORDERED  == rcvd->act.type &&
                      GCS_GROUP_PRIMARY == group->state   &&
                      group->nodes[sender_idx].status >= GCS_NODE_STATE_DONOR &&
                      !(group->frag_reset && local) &&
                      !not_commonly_supported_version)) {
            /* Common situation -
             * increment and assign act_id only for totally ordered actions
             * and only in PRIM (skip messages while in state exchange) */
            rcvd->id = ++group->act_id;
        }
        else if (GCS_ACT_TORDERED  == rcvd->act.type) {
            /* Rare situations */
            if (local) {
                /* Let the sender know that it failed */
                rcvd->id = -ERESTART;
                gu_debug("Returning -ERESTART for TORDERED action: group->state"
                         " = %s, sender->status = %s, frag_reset = %s, "
                         "buf = %p",
                         gcs_group_state_str[group->state],
                         gcs_node_state_to_str(group->nodes[sender_idx].status),
                         group->frag_reset ? "true" : "false", rcvd->act.buf);
            }
            else {
                /* Just ignore it */
                ret = 0;
                gcs_group_ignore_action (group, rcvd);
            }
        }
    }

    return ret;
}

static inline gcs_group_state_t
gcs_group_state (gcs_group_t* group)
{
    return group->state;
}

static inline bool
gcs_group_is_primary (gcs_group_t* group)
{
    return (GCS_GROUP_PRIMARY == group->state);
}

static inline int
gcs_group_my_idx (gcs_group_t* group)
{
    return group->my_idx;
}

/*!
 * Creates new configuration action
 * @param group group handle
 * @param act   GCS action object
 * @param proto protocol version gcs should use for this configuration
 */
extern ssize_t
gcs_group_act_conf (gcs_group_t* group, struct gcs_act* act, int* proto);

/*! Returns state object for state message */
extern gcs_state_msg_t*
gcs_group_get_state (gcs_group_t* group);

/*!
 * find a donor and return its index, if available. pure function.
 * @return donor index of negative error code.
 * -EHOSTUNREACH if no available donor.
 * -EHOSTDOWN if donor is joiner.
 * -EAGAIN if no node in proper state.
 */
extern int
gcs_group_find_donor(gcs_group_t* group,
                     int const str_version,
                     int const joiner_idx,
                     const char* const donor_string, int const donor_len,
                     const gu_uuid_t* ist_uuid, gcs_seqno_t ist_seqno);

#endif /* _gcs_group_h_ */
