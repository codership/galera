/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdlib.h>

#include "gcs_node.h"

#define NODE_DEFAULT_NAME  "no name"
#define NODE_DEFAULT_ADDR  "no addr"

/*! Initialize node context */
void
gcs_node_init (gcs_node_t* node, const char* id)
{
    assert(strlen(id) > 0);
    assert(strlen(id) < sizeof(node->id));

    memset (node, 0, sizeof (gcs_node_t));
    strncpy ((char*)node->id, id, sizeof(node->id) - 1);
    node->status    = GCS_STATE_NON_PRIM;
    node->name      = strdup (NODE_DEFAULT_NAME);
    node->inc_addr  = strdup (NODE_DEFAULT_ADDR);
    node->proto_min = GCS_ACT_PROTO_MIN;
    node->proto_max = GCS_ACT_PROTO_MAX;
    gcs_defrag_init (&node->app);
    gcs_defrag_init (&node->oob);
}

/*! Move data from one node object to another */
void
gcs_node_move (gcs_node_t* dst, gcs_node_t* src)
{
    if (dst->name)     free ((char*)dst->name);
    if (dst->inc_addr) free ((char*)dst->inc_addr);
    if (dst->state)    gcs_state_destroy ((gcs_state_t*)dst->state);
    memcpy (dst, src, sizeof (gcs_node_t));
    gcs_defrag_forget (&src->app);
    gcs_defrag_forget (&src->oob);
    src->name     = NULL;
    src->inc_addr = NULL;
    src->state    = NULL;
}

/*! Reset node's receive buffers */
extern void
gcs_node_reset (gcs_node_t* node) {
    gcs_defrag_free (&node->app);
    gcs_defrag_free (&node->oob);
}

/*! Deallocate resources associated with the node object */
void
gcs_node_free (gcs_node_t* node)
{
    gcs_node_reset (node);

    if (node->name) {
        free ((char*)node->name);     // was strdup'ed
        node->name = NULL;
    }
    if (node->inc_addr) {
        free ((char*)node->inc_addr); // was strdup'ed
        node->inc_addr = NULL;
    }
    if (node->state) {
        gcs_state_destroy ((gcs_state_t*)node->state);
        node->state = NULL;
    }
}

/*! Record state message from the node */
void
gcs_node_record_state (gcs_node_t* node, gcs_state_t* state)
{
    if (node->state) {
        gcs_state_destroy ((gcs_state_t*)node->state);
    }
    node->state = state;
}

/*! Update node status according to quorum decisions */
void
gcs_node_update_status (gcs_node_t* node, const gcs_state_quorum_t* quorum)
{
    if (quorum->primary) {
        const gu_uuid_t* node_group_uuid   = gcs_state_group_uuid (node->state);
        const gu_uuid_t* quorum_group_uuid = &quorum->group_uuid;

        // TODO: what to do when quorum.proto is not supported by this node?

        if (gu_uuid_compare (node_group_uuid, quorum_group_uuid)) {
            // node joins completely different group, clear all status
            node->status = GCS_STATE_PRIM;
        }
        else {
            // node was a part of this group
            gcs_seqno_t node_act_id = gcs_state_act_id (node->state);
            if (GCS_STATE_PRIM <  node->status &&
                node_act_id    != quorum->act_id) {
                // gap in sequence numbers, needs a snapshot, demote status
                node->status = GCS_STATE_PRIM;
            }
            if (GCS_STATE_PRIM > node->status) {
                // node must be at least GCS_STATE_PRIM
                node->status = GCS_STATE_PRIM;
            }
        }
        // TODO: remove when JOIN message is implemented
        node->status = GCS_STATE_JOINED;
    }
    else {
        /* Probably don't want to change anything here, quorum was a failure
         * anyway. This could be due to this being transient component, lacking
         * joined nodes from the configuraiton.
         * May be next component will be better.
         */
    }
}

