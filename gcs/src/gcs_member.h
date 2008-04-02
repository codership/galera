// Copyright (C) 2008 Codership Oy <info@codership.com>

/*
 * This header defines node specific context we need to maintain
 */

#ifndef _gcs_node_h_
#define _gcs_node_h_

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
    node_recv_act_t service;      // defragmenter for out-of-band service acts.
    gcs_comp_memb_t id;           // unique identifier provided by backend
};

typedef struct gcs_node gcs_node_t;

#endif /* _gcs_node_h_ */
