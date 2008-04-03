/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdlib.h>

#include "gcs_node.h"

/*! Move data from one node object to another */
extern void
gcs_node_move (gcs_node_t* dest, gcs_node_t* src)
{
    memcpy (dest, src, sizeof (gcs_node_t));
    gcs_recv_act_forget (&src->app);
    gcs_recv_act_forget (&src->oob);
}

/*! Deallocate resources associated with the node object */
extern void
gcs_node_free (gcs_node_t* node)
{
    // was alloc'ed by standard malloc
    gcs_recv_act_free (&node->app);
    gcs_recv_act_free (&node->oob);
}

