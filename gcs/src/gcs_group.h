/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*
 * This header defines node specific context we need to maintain
 */

#ifndef _gcs_group_h_
#define _gcs_group_h_

#include "gcs_node.h"
#include "gcs_recv_msg.h"
#include "gcs_seqno.h"

typedef enum group_state
{
    GROUP_PRIMARY,
    GROUP_NON_PRIMARY
}
group_state_t;

typedef struct gcs_group
{
    long          num;          // number of nodes
    long          my_idx;       // my index in the group
    group_state_t state;        // group state: PRIMARY | NON_PRIMARY
    bool          new_memb;     // new members in last configuration change
    volatile
    gcs_seqno_t   last_applied; // last_applied action group-wide
    long          last_node;    // node that reported last_applied
    gcs_node_t*   nodes;        // array of node contexts
}
gcs_group_t;

/*!
 * Initialized group at startup
 */
extern long
gcs_group_init (gcs_group_t* group);

/*!
 * Free group resources
 */
extern void
gcs_group_free (gcs_group_t* group);

/*!
 * Handles component message - installs new membership,
 * cleans old one.
 *
 * @return
 *        > 0 in case of success or
 *        negative error code.
 */
extern long
gcs_group_handle_comp_msg (gcs_group_t* group, gcs_comp_msg_t* comp);

/*!
 * Handles action message. Is called often - therefore, inlined
 *
 * @return to be determined
 */
static inline ssize_t
gcs_group_handle_act_msg (gcs_group_t*    group,
                          const gcs_recv_msg_t* msg,
                          gcs_recv_act_t* act)
{
    gcs_act_frag_t frg;
    register long sender_id = msg->sender_id;
    register ssize_t ret;

    assert (GCS_MSG_ACTION == msg->type);
    assert (sender_id < group->num);

    ret = gcs_act_proto_read (&frg, msg->buf, msg->size);
    if (gu_likely(!ret)) {
        ret = gcs_node_handle_act_frag (&group->nodes[sender_id],
                                        &frg, act,
                                        (sender_id == group->my_idx));
        if (gu_unlikely(ret > 0)) {
            assert (ret == act->buf_len);
            act->type      = frg.act_type;
            act->sender_id = sender_id;
        }
    }

    return ret;
}

extern gcs_seqno_t
gcs_group_handle_last_msg (gcs_group_t* group, gcs_recv_msg_t* msg);

static inline bool
gcs_group_new_members (gcs_group_t* group)
{
    return group->new_memb;
}

static inline bool
gcs_group_is_primary (gcs_group_t* group)
{
    return (GROUP_PRIMARY == group->state);
}

static inline long
gcs_group_my_idx (gcs_group_t* group)
{
    return group->my_idx;
}
#endif /* _gcs_group_h_ */
