/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to state messages - implementation
 *
 */

#include "galerautils.h"

#define GCS_STATE_ACCESS
#include "gcs_state.h"

extern gcs_state_t*
gcs_state_create (gcs_seqno_t act_id,
                  gcs_seqno_t comp_id,
                  gcs_seqno_t conf_id,
                  bool        joined,
                  long        prim_idx,
                  const char* name,
                  const char* inc_addr,
                  gcs_proto_t proto_min,
                  gcs_proto_t proto_max)
{
    size_t name_len  = strlen(name) + 1;
    size_t addr_len  = strlen(inc_addr) + 1;
    gcs_state_t* ret =
        gu_calloc (1, sizeof (gcs_state_t) + name_len + addr_len);
    if (ret) {
        ret->act_id    = act_id;
        ret->comp_id   = comp_id;
        ret->conf_id   = conf_id;
        ret->joined    = joined;
        ret->prim_idx  = prim_idx;
        ret->proto_min = proto_min;
        ret->proto_max = proto_max;
        ret->name      = (void*)(ret) + sizeof (gcs_state_t);
        ret->inc_addr  = ret->name + name_len;
        strcpy ((char*)ret->name, name);
        strcpy ((char*)ret->inc_addr, inc_addr);
    }
    return ret;
}

extern void
gcs_state_destroy (gcs_state_t* state)
{
    gu_free (state);
}

/* Returns length needed to serialize gcs_state_msg_t for sending */
extern ssize_t
gcs_state_msg_len (gcs_state_t* state)
{
    return (
        1 + // proto_min
        1 + // proto_max
        2 + // flags (joined, etc.)
        8 + // act_id
        8 + // comp_id
        8 + // conf_id
        4 + // prim_idx
        strlen (state->name) + 1 +
        strlen (state->inc_addr) + 1
        );
}

#define STATE_MSG_FLAG_JOINED    0x01

#define STATE_MSG_FIELDS(_const,buf)                   \
    _const uint8_t*  proto_min = (buf);                      \
    _const uint8_t*  proto_max = proto_min + 1;              \
    _const uint16_t* flags     = (uint16_t*)(proto_max + 1); \
    _const uint64_t* act_id    = (uint64_t*)(flags + 1);     \
    _const uint64_t* comp_id   = act_id + 1;                 \
    _const uint64_t* conf_id   = comp_id + 1;                \
    _const uint32_t* prim_idx  = (uint32_t*)(conf_id + 1);   \
    _const char*     name      = (char*)(prim_idx + 1);

/* Serialize gcs_state_msg_t into buf */
extern ssize_t
gcs_state_msg_write (void* buf, const gcs_state_t* state)
{
    STATE_MSG_FIELDS(,buf);
    char*     inc_addr  = name + strlen (state->name) + 1;

    *proto_min = state->proto_min;
    *proto_max = state->proto_max;
    *flags     = gu_le16(state->joined == true ? STATE_MSG_FLAG_JOINED : 0);
    *act_id    = gu_le64(state->act_id);
    *comp_id   = gu_le64(state->comp_id);
    *conf_id   = gu_le64(state->conf_id);
    *prim_idx  = gu_le32(state->prim_idx);
    strcpy (name,     state->name);
    strcpy (inc_addr, state->inc_addr);

    return (inc_addr + strlen(inc_addr) + 1 - (char*)buf);
}

/* De-serialize gcs_state_msg_t from buf */
extern gcs_state_t*
gcs_state_msg_read (const void* buf, size_t buf_len)
{
    STATE_MSG_FIELDS(const,buf);
    const char* inc_addr = name + strlen (name) + 1;
    bool        joined   = (gu_le16(*flags) & STATE_MSG_FLAG_JOINED) != 0;

    return gcs_state_create (
        gu_le64(*act_id),
        gu_le64(*comp_id),
        gu_le64(*conf_id),
        joined,
        gu_le32(*prim_idx),
        name,
        inc_addr,
        *proto_min,
        *proto_max
        );
}

/* Print state message contents to buffer */
extern int
gcs_state_snprintf (char* str, size_t size, const gcs_state_t* state)
{
    return snprintf (str, size,
                     "Protocols    : %c-%c\n"
                     "Global seqno : %llu\n"
                     "Configuration: %llu\n"
                     "Component    : %llu\n"
                     "Joined       : %s\n"
                     "Last index   : %ld\n"
                     "Name         : '%s'\n"
                     "Incoming addr: '%s'\n",
                     state->proto_min, state->proto_max,
                     state->act_id, state->conf_id, state->comp_id,
                     state->joined ? "yes" : "no",
                     state->prim_idx,
                     state->name,
                     state->inc_addr
        );
}

