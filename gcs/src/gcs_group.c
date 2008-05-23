/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <errno.h>
#include <galerautils.h>

#include "gcs_group.h"

long
gcs_group_init (gcs_group_t* group)
{
    group->num          = 0;
    group->my_idx       = -1;
    group->state        = GROUP_NON_PRIMARY;
    group->last_applied = GCS_SEQNO_ILL; // mark for recalculation
    group->last_node    = -1;
    group->nodes        = NULL;
    return 0;
}

/* Initialize nodes array */
static inline gcs_node_t*
group_nodes_init (gcs_comp_msg_t* comp)
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

    for (n = 1; n < group->num; n++) {
        gcs_seqno_t seqno = gcs_node_get_last_applied (&group->nodes[n]);
        if (seqno < group->last_applied) {
            group->last_applied = seqno;
            group->last_node    = n;
        }
    }
}

// NOTE: new_memb should be cleared only after handling SYNC message
long
gcs_group_handle_comp_msg (gcs_group_t* group, gcs_comp_msg_t* comp)
{
    long        new_idx, old_idx;
    long        new_nodes_num = 0;
    gcs_node_t *new_nodes = NULL;

    gu_debug ("primary = %s, my_id = %d, memb_num = %d, group_state = %d",
	      gcs_comp_msg_primary(comp) ? "yes" : "no",
	      gcs_comp_msg_self(comp), gcs_comp_msg_num (comp), group->state);

    if (gcs_comp_msg_primary(comp)) {
	/* Got PRIMARY COMPONENT - Hooray! */
	/* create new nodes array according to new membrship */
	new_nodes_num = gcs_comp_msg_num (comp);
	new_nodes     = group_nodes_init (comp);
	if (!new_nodes) return -ENOMEM;
	
	if (group->state == GROUP_PRIMARY) {
	    /* we come from previous primary configuration */
	    /* remap old array to new one to preserve action continuity */
	    assert (group->nodes);
	    gu_debug ("\tMembers:");
	    for (new_idx = 0; new_idx < new_nodes_num; new_idx++) {
		/* find member index in old component by unique member id */
                for (old_idx = 0; old_idx < group->num; old_idx++) {
                    // just scan through old group
                    if (!strcmp(group->nodes[old_idx].id,
                                new_nodes[new_idx].id)) {
                        /* the node was in previous configuration with us */
                        /* move node context to new node array */
                        gcs_node_move (&new_nodes[new_idx],
                                       &group->nodes[old_idx]);
                        break;
                    }
                }
                /* if wasn't found in new configuration, new member -
                 * need to resend actions in process */
                group->new_memb |= (old_idx == group->num);
	    }
	}
	else {
	    /* It happened so that we missed some primary configurations */
	    gu_warn ("Discontinuity in primary configurations!");
	    gu_warn ("State snapshot is needed!");
            /* we can't go to PRIMARY without joining some other PRIMARY guys */
            group->new_memb |= 1;
	    group->state = GROUP_PRIMARY;
	}
    }
    else {
	/* Got NON-PRIMARY COMPONENT - cleanup */
	if (group->state == GROUP_PRIMARY) {
	    /* All sending threads must be aborted with -ENOTGROUP,
	     * local action FIFO must be flushed. Not implemented: FIXME! */
	    group->state = GROUP_NON_PRIMARY;
	}
    }

    /* free old nodes array */
    group_nodes_free (group);

    group->nodes  = new_nodes;
    group->num    = new_nodes_num;
    group->my_idx = gcs_comp_msg_self (comp);

    if (group->num > 0) {
        /* if new nodes joined, reset ongoing actions */
        if (group->new_memb) {
            group_nodes_reset (group);
        }
        group_redo_last_applied (group);
    }

    return group->state;
}

gcs_seqno_t
gcs_group_handle_last_msg (gcs_group_t* group, gcs_recv_msg_t* msg)
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

void
gcs_group_free (gcs_group_t* group)
{
    group_nodes_free (group);
}
