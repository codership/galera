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
    group->nodes        = NULL;
    return 0;
}

/* Initialize nodes array */
static inline gcs_node_t*
group_nodes_init (long nodes_num)
{
    gcs_node_t* ret = NULL;
    register long i;

    ret = GU_CALLOC (nodes_num, gcs_node_t);
    if (ret) {
        for (i = 0; i < nodes_num; i++) {
            gcs_node_init (&ret[i]);
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

long
gcs_group_handle_comp_msg (gcs_group_t* group, gcs_comp_msg_t* comp)
{
    long        new_idx, old_idx;
    long        new_nodes_num = 0;
    gcs_node_t *new_nodes = NULL;

    gu_debug ("primary = %s, my_id = %d, memb_num = %d, group_state = %d",
	      gcs_comp_msg_primary(comp) ? "yes" : "no",
	      gcs_comp_msg_self(comp), gcs_comp_msg_num (comp), group->state);

    group->new_memb = 0;

    if (gcs_comp_msg_primary(comp)) {
	/* Got PRIMARY COMPONENT - Hooray! */
	/* create new nodes array according to new membrship */
	new_nodes_num = gcs_comp_msg_num(comp);
	new_nodes     = group_nodes_init (new_nodes_num);
	if (!new_nodes) return -ENOMEM;
	
	if (group->state == GROUP_PRIMARY) {
	    /* we come from previous primary configuration */
	    /* remap old array to new one to preserve action continuity */
	    assert (group->nodes);
	    gu_debug ("\tMembers:");
	    for (new_idx = 0; new_idx < new_nodes_num; new_idx++) {
		/* find member index in old component by unique member id */
                const char* new_id = gcs_comp_msg_id (comp, new_idx);
		gu_debug ("\t%s", new_id);
                
                for (old_idx = 0; old_idx < group->num; old_idx++) {
                    // just scan through old group
                    if (!strcmp(group->nodes[old_idx].id, new_id)) {
                        /* the node was in previous configuration with us */
                        /* move node context to new node array */
                        gcs_node_move (&new_nodes[new_idx],
                                       &group->nodes[old_idx]);
                        continue;
                    }
                    /* wasn't found in new configuration, new member -
                     * need to resend actions in process */
                    group->new_memb |= 1;
                }
	    }
	}
	else {
	    /* It happened so that we missed some primary configurations */
	    gu_warn ("Discontinuity in primary configurations!");
	    gu_warn ("State snapshot is needed!");
	    group->state = GROUP_PRIMARY;
            /* we can't go to PRIMARY without joining some other PRIMARY guys */
            group->new_memb |= 1;
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

    /* if new nodes joined, reset ongoing actions */
    group_nodes_reset (group);

    return group->state;
}

void
gcs_group_free (gcs_group_t* group)
{
    group_nodes_free (group);
}
