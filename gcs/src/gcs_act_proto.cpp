/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to action protocol
 * (to be extended to support protocol versions, currently supports only v0)
 */
#include <errno.h>
#include "gcs_act_proto.hpp"

/*

  Version 0 header structure

bytes: 00 01                07 08       11 12       15 16       19 20
      +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+---
      |PV|      act_id        |  act_size |  frag_no  |AT|reserved|  data...
      +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+---

PV - protocol version
AT - action type

*/

static const size_t PROTO_PV_OFFSET       = 0;
static const size_t PROTO_AT_OFFSET       = 16;
static const size_t PROTO_DATA_OFFSET     = 20;
// static const size_t PROTO_ACT_ID_OFFSET   = 0;
// static const size_t PROTO_ACT_SIZE_OFFSET = 8;
// static const size_t PROTO_FRAG_NO_OFFSET  = 12;

// static const gcs_seqno_t   PROTO_ACT_ID_MAX   = 0x00FFFFFFFFFFFFLL;
// static const unsigned int  PROTO_FRAG_NO_MAX  = 0xFFFFFFFF;
// static const unsigned char PROTO_AT_MAX       = 0xFF;

#define PROTO_MAX_HDR_SIZE PROTO_DATA_OFFSET // for now

/*! Writes header data into actual header of the message.
 *  Remainig fragment buf and length is in frag->frag and frag->frag_len
 *
 * @return 0 on success */
long
gcs_act_proto_write (gcs_act_frag_t* frag, void* buf, size_t buf_len)
{
#ifdef GCS_DEBUG_PROTO
    if ((frag->act_id   > PROTO_ACT_ID_MAX)  ||
        (frag->act_size > GCS_MAX_ACT_SIZE)  ||
        (frag->frag_no  > PROTO_FRAG_NO_MAX) ||
        (frag->act_type > PROTO_AT_MAX)) {
        gu_error ("Exceeded protocol limits: %d(%d), %d(%d), %d(%d), %d(%d)",
                  frag->act_id,   PROTO_ACT_ID_MAX,
                  frag->act_size, GCS_MAX_ACT_SIZE,
                  frag->frag_no,  PROTO_FRAG_NO_MAX,
                  frag->act_type, PROTO_AT_MAX);
        return -EOVERFLOW;
    }
    if (frag->proto_ver > GCS_PROTO_MAX) return -EPROTO;
    if (buf_len     < PROTO_DATA_OFFSET) return -EMSGSIZE;
#endif

    // assert (frag->act_size <= PROTO_ACT_SIZE_MAX);

    ((uint64_t*)buf)[0] = gu_be64(frag->act_id);
    ((uint32_t*)buf)[2] = htogl  ((uint32_t)frag->act_size);
    ((uint32_t*)buf)[3] = htogl  (frag->frag_no);

    ((uint8_t *)buf)[PROTO_PV_OFFSET] = frag->proto_ver;
    ((uint8_t *)buf)[PROTO_AT_OFFSET] = frag->act_type;

    frag->frag     = (uint8_t*)buf + PROTO_DATA_OFFSET;
    frag->frag_len = buf_len - PROTO_DATA_OFFSET;

    return 0;
}

/*! Reads header data from the actual header of the message
 *  Remainig fragment buf and length is in frag->frag and frag->frag_len
 *
 * @return 0 on success */
long
gcs_act_proto_read (gcs_act_frag_t* frag, const void* buf, size_t buf_len)
{
    frag->proto_ver = ((uint8_t*)buf)[PROTO_PV_OFFSET];

    if (gu_unlikely(buf_len < PROTO_DATA_OFFSET)) {
        gu_error ("Action message too short: %zu, expected at least %d",
                  buf_len, PROTO_DATA_OFFSET);
        return -EBADMSG;
    }

    if (gu_unlikely(frag->proto_ver > GCS_PROTO_MAX)) {
        gu_error ("Bad protocol version %d, maximum supported %d",
                  frag->proto_ver, GCS_PROTO_MAX);
        return -EPROTO; // this fragment should be dropped
    }

    ((uint8_t*)buf)[PROTO_PV_OFFSET] = 0x0;
    frag->act_id   = gu_be64(*(uint64_t*)buf);
    frag->act_size = gtohl  (((uint32_t*)buf)[2]);
    frag->frag_no  = gtohl  (((uint32_t*)buf)[3]);
    frag->act_type = static_cast<gcs_act_type_t>(
        ((uint8_t*)buf)[PROTO_AT_OFFSET]);
    frag->frag     = ((uint8_t*)buf) + PROTO_DATA_OFFSET;
    frag->frag_len = buf_len - PROTO_DATA_OFFSET;

    /* return 0 or -EMSGSIZE */
    return ((frag->act_size > GCS_MAX_ACT_SIZE) * -EMSGSIZE);
}

/*! Returns protocol header size */
long
gcs_act_proto_hdr_size (long version)
{
    if (gu_unlikely(GCS_PROTO_MAX < version)) return -EPROTONOSUPPORT;

    if (gu_unlikely(version < 0)) return PROTO_MAX_HDR_SIZE; // safe

    return PROTO_DATA_OFFSET;
}

