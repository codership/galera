/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to action protocol
 * (to be extended to support protocol versions, currently supports only v0)
 */

#ifndef _gcs_act_proto_h_
#define _gcs_act_proto_h_

#include "gcs.hpp" // for gcs_seqno_t

#include <galerautils.h>
#include <stdint.h>
typedef uint8_t gcs_proto_t;

/*! Supported protocol range (for now only version 0 is supported) */
#define GCS_ACT_PROTO_MAX 0

/*! Internal action fragment data representation */
typedef struct gcs_act_frag
{
    gcs_seqno_t    act_id;
    size_t         act_size;
    const void*    frag;     // shall override it only once
    size_t         frag_len;
    unsigned long  frag_no;
    gcs_act_type_t act_type;
    int            proto_ver;
}
gcs_act_frag_t;

/*! Writes header data into actual header of the message.
 *  Remainig fragment buf and length is in frag->frag and frag->frag_len */
extern long
gcs_act_proto_write (gcs_act_frag_t* frag, void* buf, size_t buf_len);

/*! Reads header data from the actual header of the message
 *  Remainig fragment buf and length is in frag->frag and frag->frag_len */
extern long
gcs_act_proto_read (gcs_act_frag_t* frag, const void* buf, size_t buf_len);

/*! Increments fragment counter when action remains the same.
 *
 * @return non-negative counter value on success
 */
static inline long
gcs_act_proto_inc (void* buf)
{
    uint32_t frag_no = gtohl(((uint32_t*)buf)[3]) + 1;
#ifdef GCS_DEBUG_PROTO
    if (!frag_no) return -EOVERFLOW;
#endif
    ((uint32_t*)buf)[3] = htogl(frag_no);
    return frag_no;
}

/*! Returns protocol header size */
extern long
gcs_act_proto_hdr_size (long version);

/*! Returns message protocol version */
static inline int
gcs_act_proto_ver (void* buf)
{
    return *((uint8_t*)buf);
}

#endif /* _gcs_act_proto_h_ */
