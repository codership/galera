/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Receiving action context
 */

#ifndef _gcs_recv_act_h_
#define _gcs_recv_act_h_

typedef struct gcs_recv_act
{
    gcs_seqno_t    send_no; // send id (unique for a node)
    uint8_t*       head;    // head of action buffer
    uint8_t*       tail;    // tail of action data
    size_t         size;
    size_t         received;
    gcs_act_type_t type;
}
gcs_recv_act_t;

/*! Deassociate, but don't deallocate action resources */
static inline void
gcs_recv_act_forget (gcs_recv_act_t* act)
{
    act->head = NULL;
}

/*! Free resources associated with the action */
static inline void
gcs_recv_act_free (gcs_recv_act_t* act)
{
    free (act->head); // alloc'ed with standard malloc
    act->head = NULL;
}

#endif /* _gcs_recv_act_h_ */
