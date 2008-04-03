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

#include <string.h>   // for memset
#include <stdbool.h>
#include <galerautils.h>

#include "gcs_recv_msg.h"

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

/*!
 * Handle received message - action fragment
 *
 * @return 0              - success,
 *         size of action - success, full action received,
 *         negative       - error.
 */
extern long
gcs_recv_act_handle_msg (gcs_recv_act_t*       act,
                         const gcs_recv_msg_t* msg,
                         bool                  foreign);

/*!
 * Pop received action buffer and get ready to receive another
 *
 * @return pointer to action buffer, NULL if action is local - must be
 *         fetched from local fifo.
 */
static inline uint8_t*
core_pop_action (gcs_recv_act_t* act)
{
    register uint8_t* ret = act->head;

    assert (act->size == act->received);

    memset (act, 0, sizeof (*act));
    act->send_no = GCS_SEQNO_ILL;

    return ret;
}


/*! Deassociate, but don't deallocate action resources */
static inline void
gcs_recv_act_forget (gcs_recv_act_t* act)
{
    act->head = NULL;
}

/*! Free resources associated with the action (for lost node cleanup) */
static inline void
gcs_recv_act_free (gcs_recv_act_t* act)
{
    free (act->head); // alloc'ed with standard malloc
    act->head = NULL;
}

#endif /* _gcs_recv_act_h_ */
