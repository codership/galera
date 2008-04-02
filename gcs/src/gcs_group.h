// Copyright (C) 2008 Codership Oy <info@codership.com>

/*
 * This header defines node specific context we need to maintain
 */

#ifndef _gcs_group_h_
#define _gcs_group_h_

#include "gcs_node.h"

typedef struct gcs_group
{
    long        num;   // number of nodes
    gcs_node_t* nodes; // array of node contexts
}
gcs_group_t;

#endif /* _gcs_group_h_ */
