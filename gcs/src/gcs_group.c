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

long
gcs_group_handle_comp_msg (gcs_group_t* group, gcs_comp_msg_t* comp)
{
    long new_idx, old_idx;
    long new_nodes_num = 0;
    gcs_node_t *new_nodes = NULL;

    gu_debug ("primary = %s, my_id = %d, memb_num = %d, group_state = %d",
	      gcs_comp_msg_primary(comp) ? "yes" : "no",
	      gcs_comp_msg_self(comp), gcs_comp_msg_num (comp), group->state);

    if (gcs_comp_msg_primary(comp)) {
	/* Got PRIMARY COMPONENT - Hooray! */
	/* create new nodes array according to new membrship */
	new_nodes_num = gcs_comp_msg_num(comp);
	new_nodes     = GU_CALLOC (new_nodes_num, gcs_node_t);
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
                    }
                }
	    }
	}
	else {
	    /* It happened so that we missed some primary configurations */
	    gu_debug ("Discontinuity in primary configurations!");
	    gu_debug ("State snapshot is needed!");
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

    /* free actions that were left from disappeared members */
    for (old_idx = 0; old_idx < group->num; old_idx++) {
        gcs_node_cleanup (&group->nodes[old_idx]);
    }

    /* replace old component data with new one
     * (recv_acts is null when first primary configuration comes) */
    if (group->nodes) gu_free (group->nodes);

    group->nodes  = new_nodes;
    group->num    = new_nodes_num;
    group->my_idx = gcs_comp_msg_self (comp);

    return group->state;
}
