/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _gcs_act_h_
#define _gcs_act_h_

#include "gcs.hpp"

struct gcs_act
{
    const void*    buf;
    ssize_t        buf_len;
    gcs_act_type_t type;
    gcs_act() : buf(NULL), buf_len(0), type(GCS_ACT_ERROR) { }
    gcs_act(const void* b, ssize_t bl, gcs_act_type_t t)
        :
        buf(b),
        buf_len(bl),
        type(t)
    { }
    gcs_act(const gcs_act& a)
        :
        buf(a.buf),
        buf_len(a.buf_len),
        type(a.type)
    { }
};

struct gcs_act_rcvd
{
    struct gcs_act act;
    const struct gu_buf* local; // local buffer vector if any
    gcs_seqno_t    id;          // global total order seqno
    int            sender_idx;
    gcs_act_rcvd() : act(), local(NULL), id(GCS_SEQNO_ILL), sender_idx(-1) { }
    gcs_act_rcvd(const gcs_act& a, const struct gu_buf* loc,
                 gcs_seqno_t i, int si)
        :
        act(a),
        local(loc),
        id(i),
        sender_idx(si)
    { }
};

#endif /* _gcs_act_h_ */
