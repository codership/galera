/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Node context
 */

#ifndef _gcs_node_h_
#define _gcs_node_h_

#include "gcs.h"
#include "gcs_recv_act.h"
#include "gcs_comp_msg.h"

struct gcs_node
{
    gcs_seqno_t    last_applied; // last applied action on that node
    long           queue_len;    // action queue length on that node
    gcs_recv_act_t app;          // defragmenter for application actions
    gcs_recv_act_t oob;          // defragmenter for out-of-band service acts.

    // globally unique id from the component message
    const char      id[GCS_COMP_MEMB_ID_MAX_LEN + 1];
};

typedef struct gcs_node gcs_node_t;

/*! Move data from one node object to another */
extern void
gcs_node_move (gcs_node_t* dest, gcs_node_t* src);

/*! Deallocate resources associated with the node object */
extern void
gcs_node_free (gcs_node_t* node);

#endif /* _gcs_node_h_ */
