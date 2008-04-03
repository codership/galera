/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*
 * This header defines node specific context we need to maintain
 */

#ifndef _gcs_node_h_
#define _gcs_node_h_

#include "gcs.h"

#include "gcs_comp_msg.h"

typedef struct node_recv_act
{
    gcs_seqno_t    send_no;
    uint8_t*       head; // head of action buffer
    uint8_t*       tail; // tail of action data
    size_t         size;
    size_t         received;
    gcs_act_type_t type;
}
node_recv_act_t;

struct gcs_node
{
    gcs_seqno_t     last_applied; // last applied action on that node
    long            queue_len;    // action queue length on that node
    node_recv_act_t app;          // defragmenter for application actions
    node_recv_act_t oob;          // defragmenter for out-of-band service acts.

    // globally unique id from the component message
    const char      id[GCS_COMP_MEMB_ID_MAX_LEN + 1];
};

typedef struct gcs_node gcs_node_t;

/*! Move data from one node object to another */
extern void
gcs_node_move (gcs_node_t* dest, gcs_node_t* src);

/*! Deallocate resources associated with the node object */
extern void
gcs_node_cleanup (gcs_node_t* node);

#endif /* _gcs_node_h_ */
