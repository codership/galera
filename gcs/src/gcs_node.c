// Copyright (C) 2008 Codership Oy <info@codership.com>

#include <stdlib.h>

#include "gcs_node.h"

/*! Move data from one node object to another */
extern void
gcs_node_move (gcs_node_t* dest, gcs_node_t* src)
{
    memcpy (dest, src, sizeof (gcs_node_t));
    src->app.head = NULL;
    src->oob.head = NULL;
}

/*! Deallocate resources associated with the node object */
extern void
gcs_node_cleanup (gcs_node_t* node)
{
    // was alloc'ed by standard malloc
    free (node->app.head); node->app.head = NULL;
    free (node->oob.head); node->oob.head = NULL;
}

