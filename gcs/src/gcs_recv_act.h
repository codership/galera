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

#include "gcs.h"

typedef struct gcs_recv_act
{
    gcs_seqno_t    id;        // total order seqno
    const void*    buf;
    ssize_t        buf_len;
    gcs_act_type_t type;
    long           sender_idx;    
}
gcs_recv_act_t;

#endif /* _gcs_recv_act_h_ */
