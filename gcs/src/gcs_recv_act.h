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

#include "gcs_act_type.h"

typedef struct gcs_recv_act
{
    void*          buf;
    size_t         buf_len;
    gcs_act_type_t size;
    long           sender_id;    
}
gcs_recv_act_t;

#endif /* _gcs_recv_act_h_ */
