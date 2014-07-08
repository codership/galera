/*
 * Copyright (C) 2008-2011 Codership Oy <info@codership.com>
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
    gcs_act() { }
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
    int            proto_ver;
    gcs_act_rcvd() { }
    gcs_act_rcvd(const gcs_act& a, const struct gu_buf* loc,
                 gcs_seqno_t i, int si, int pv)
        :
        act(a),
        local(loc),
        id(i),
        sender_idx(si),
        proto_ver(pv)
    { }
};

#endif /* _gcs_act_h_ */
