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
gcs_state_create (const gu_uuid_t* state_uuid,
                  const gu_uuid_t* group_uuid,
                  gcs_seqno_t      act_id,
                  gcs_seqno_t      conf_id,
                  gcs_state_node_t status,
                  const char*      name,
                  const char*      inc_addr,
                  gcs_proto_t      proto_min,
                  gcs_proto_t      proto_max)
{
    size_t name_len  = strlen(name) + 1;
    size_t addr_len  = strlen(inc_addr) + 1;
    gcs_state_t* ret =
        gu_calloc (1, sizeof (gcs_state_t) + name_len + addr_len);
    if (ret) {
        ret->state_uuid = *state_uuid;
        ret->group_uuid = *group_uuid;
        ret->act_id     = act_id;
        ret->conf_id    = conf_id;
        ret->status     = status;
        ret->proto_min  = proto_min;
        ret->proto_max  = proto_max;
        ret->name       = (void*)(ret) + sizeof (gcs_state_t);
        ret->inc_addr   = ret->name + name_len;
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
        sizeof (uint8_t)     +   // version (reserved)
        sizeof (uint8_t)     +   // proto_min
        sizeof (uint8_t)     +   // proto_max
        sizeof (uint8_t)     +   // node status
        sizeof (gu_uuid_t)   +   // state exchange UUID
        sizeof (gu_uuid_t)   +   // group UUID
        sizeof (gcs_seqno_t) +   // act_id
        sizeof (gcs_seqno_t) +   // conf_id
        strlen (state->name) + 1 +
        strlen (state->inc_addr) + 1
        );
}

#define STATE_MSG_FIELDS_V0(_const,buf)                         \
    _const uint8_t*   version    = (buf);                       \
    _const uint8_t*   proto_min  = version   + 1;               \
    _const uint8_t*   proto_max  = proto_min + 1;               \
    _const uint8_t*   status     = proto_max + 1;               \
    _const gu_uuid_t* state_uuid = (gu_uuid_t*)(status + 1);    \
    _const gu_uuid_t* group_uuid = state_uuid + 1;              \
    _const uint64_t*  act_id     = (uint64_t*)(group_uuid + 1); \
    _const uint64_t*  conf_id    = act_id + 1;                  \
    _const char*      name       = (char*)(conf_id + 1);

/* Serialize gcs_state_msg_t into buf */
extern ssize_t
gcs_state_msg_write (void* buf, const gcs_state_t* state)
{
    STATE_MSG_FIELDS_V0(,buf);
    char*     inc_addr  = name + strlen (state->name) + 1;

    *version    = 0;
    *proto_min  = state->proto_min;
    *proto_max  = state->proto_max;
    *status     = state->status;
    *state_uuid = state->state_uuid;
    *group_uuid = state->group_uuid;
    *act_id     = gu_le64(state->act_id);
    *conf_id    = gu_le64(state->conf_id);
    strcpy (name,     state->name);
    strcpy (inc_addr, state->inc_addr);

    return (inc_addr + strlen(inc_addr) + 1 - (char*)buf);
}

/* De-serialize gcs_state_msg_t from buf */
extern gcs_state_t*
gcs_state_msg_read (const void* buf, size_t buf_len)
{
    switch (*((uint8_t*)buf)) {
    case 0: {
        STATE_MSG_FIELDS_V0(const,buf);
        const char* inc_addr = name + strlen (name) + 1;

        return gcs_state_create (
            state_uuid,
            group_uuid,
            gu_le64(*act_id),
            gu_le64(*conf_id),
            *status,
            name,
            inc_addr,
            *proto_min,
            *proto_max
            );
    }
    default:
        gu_error ("Unrecognized state message v. %u", *(uint8_t*)buf);
        return NULL;
    }
}

static const char* state_node_status[] =
{
    "Non-primary",
    "Primary",
    "Joined",
    "Synced",
    "Donor"
};

/* Print state message contents to buffer */
extern int
gcs_state_snprintf (char* str, size_t size, const gcs_state_t* state)
{
    return snprintf (str, size,
                     "Protocols    : %u-%u\n"
                     "Status       : %s\n"
                     "Global seqno : %llu\n"
                     "Configuration: %llu\n"
                     "State UUID   : "GU_UUID_FORMAT"\n"
                     "Group UUID   : "GU_UUID_FORMAT"\n"
                     "Name         : '%s'\n"
                     "Incoming addr: '%s'\n",
                     state->proto_min, state->proto_max,
                     state_node_status[state->status],
                     state->act_id, state->conf_id,
                     GU_UUID_ARGS(&state->state_uuid),
                     GU_UUID_ARGS(&state->group_uuid),
                     state->name,
                     state->inc_addr
        );
}

