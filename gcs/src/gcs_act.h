/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _gcs_act_h_
#define _gcs_act_h_

#include "gcs.h"

struct gcs_act
{
    const void*    buf;
    ssize_t        buf_len;
    gcs_act_type_t type;
};

struct gcs_act_rcvd
{
    struct gcs_act        act;
    const struct gcs_buf* local; // local buffer vector if any
    gcs_seqno_t           id;    // global total order seqno
    int                   sender_idx;
};

#endif /* _gcs_act_h_ */
