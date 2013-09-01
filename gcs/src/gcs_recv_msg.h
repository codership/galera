/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * Receiving message context
 */

#ifndef _gcs_recv_msg_h_
#define _gcs_recv_msg_h_

#include "gcs_msg_type.h"

typedef struct gcs_recv_msg
{
    void*          buf;
    int            buf_len;
    int            size;
    int            sender_idx;
    gcs_msg_type_t type;
}
gcs_recv_msg_t;

#endif /* _gcs_recv_msg_h_ */
