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
    gcs_seqno_t   last_applied; // last_applied action group-wide
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
 *        GROUP_PRIMARY or GROUP_NON_PRIMARY in case of success or
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
gcs_group_handle_msg (gcs_group_t*    group,
                      gcs_recv_msg_t* msg,
                      gcs_recv_act_t* act)
{
    if (gu_likely(GCS_MSG_ACTION == msg->type)) {
        gcs_act_frag_t frg;
        register long  ret = gcs_act_proto_read (&frg, msg->buf, msg->size);

        if (gu_likely(!ret)) {
            return gcs_node_handle_act_frag (&group->nodes[msg->sender_id],
                                             &frg, act,
                                             (msg->sender_id == group->my_idx));
        }
        else {
            return ret;
        }
    }
    else {
        return -EBADMSG; // expected action message
    }
}
#endif /* _gcs_group_h_ */
