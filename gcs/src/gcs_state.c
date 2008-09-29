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

gcs_state_t*
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

void
gcs_state_destroy (gcs_state_t* state)
{
    gu_free (state);
}

/* Returns length needed to serialize gcs_state_msg_t for sending */
size_t
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
ssize_t
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
gcs_state_t*
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
int
gcs_state_snprintf (char* str, size_t size, const gcs_state_t* state)
{
    str[size - 1] = '\0'; // preventive termination
    return snprintf (str, size - 1,
                     "\n\tProtocols    : %u-%u"
                     "\n\tStatus       : %s"
                     "\n\tGlobal seqno : %lld"
                     "\n\tConfiguration: %lld"
                     "\n\tState UUID   : "GU_UUID_FORMAT
                     "\n\tGroup UUID   : "GU_UUID_FORMAT
                     "\n\tName         : '%s'"
                     "\n\tIncoming addr: '%s'\n",
                     state->proto_min, state->proto_max,
                     state_node_status[state->status],
                     state->act_id, state->conf_id,
                     GU_UUID_ARGS(&state->state_uuid),
                     GU_UUID_ARGS(&state->group_uuid),
                     state->name,
                     state->inc_addr
        );
}

/* Get state uuid */
const gu_uuid_t*
gcs_state_uuid (const gcs_state_t* state)
{
    return &state->state_uuid;
}

/* Get group uuid */
const gu_uuid_t*
gcs_state_group_uuid (const gcs_state_t* state)
{
    return &state->group_uuid;
}

/* Get action seqno */
gcs_seqno_t
gcs_state_act_id (const gcs_state_t* state)
{
    return state->act_id;
}

/* Returns the node which is more representative of a group */
static const gcs_state_t*
state_nodes_compare (const gcs_state_t* left, const gcs_state_t* right)
{
    assert (0 == gu_uuid_compare(&left->group_uuid, &right->group_uuid));
    assert (left->conf_id  != GCS_SEQNO_ILL);
    assert (right->conf_id != GCS_SEQNO_ILL);

    if (left->act_id < right->act_id) {
        return right;
    }
    else if (left->act_id > right->act_id) {
        return left;
    }
    else {
        // act_id's are equal, choose the one with higher conf_id.
        if ((int64_t)left->conf_id < (int64_t)right->conf_id) {
            return right;
        }
        else {
            return left;
        }
    }
}

/* Prints out all significant (JOINED) nodes */
static void
state_report_conflicting_uuids (const gcs_state_t* states[], long states_num)
{
    long j;
    for (j = 0; j < states_num; j++) {
        if (states[j]->conf_id != GCS_SEQNO_ILL &&
            states[j]->status  >= GCS_STATE_JOINED) {
            size_t st_len = 1024;
            char   st[st_len];
            gcs_state_snprintf (st, st_len, states[j]);
            st[st_len - 1] = '\0';
            gu_fatal ("%s", st);
        }        
    }
}

/* Get quorum decision from state messages */
long 
gcs_state_get_quorum (const gcs_state_t*  states[],
                      long                states_num,
                      gcs_state_quorum_t* quorum)
{
    /* We count only nodes which come from primary configuraton -
     * conf_id != GCS_SEQNO_ILL
     * They all must have the same group_uuid or otherwise quorum is impossible.
     * Of those we need to find at least one that has complete state - 
     * status >= GCS_STATE_JOINED. If we find none - configuration is
     * non-primary.
     * Of those with the status >= GCS_STATE_JOINED we choose the most
     * representative: with the highest act_id and conf_id.
     */

    long i, j;
    const gcs_state_t* rep = NULL;
    gcs_state_quorum_t GCS_STATE_QUORUM_NON_PRIMARY =
        {
            GU_UUID_NIL,
            GCS_SEQNO_ILL,
            GCS_SEQNO_ILL,
            false,
            -1
        };

    *quorum = GCS_STATE_QUORUM_NON_PRIMARY; // pessimistic assumption

    // find at least one JOINED
    for (i = 0; i < states_num; i++) {
        if (states[i]->conf_id >= 0 &&
            states[i]->status  >= GCS_STATE_JOINED) {
            rep = states[i];
            break;
        }
    }

    if (!rep) {
        gu_debug ("No node with complete state");
        return 0;
    }

    // Check that all JOINED have the same group UUID and find most updated
    for (j = i+1; j < states_num; j++) {
        if (states[j]->conf_id != GCS_SEQNO_ILL &&
            states[j]->status  >= GCS_STATE_JOINED) {
            if (gu_uuid_compare (&rep->group_uuid, &states[i]->group_uuid)) {
                // for now just freak out and print all conflicting nodes
                gu_fatal ("Quorum impossible: conflicting group UUIDs:");
                state_report_conflicting_uuids (states, states_num);
                return 0;
            }
            rep = state_nodes_compare (rep, states[i]);
        }
    }

    // select the most suitable protocol
    quorum->proto = rep->proto_max;
    for (i = 0; i < states_num; i++) {
        if (states[i]->proto_max <  quorum->proto &&
            states[i]->proto_max >= rep->proto_min) {
            quorum->proto = states[i]->proto_max;
        }
    }

    quorum->act_id     = rep->act_id;
    quorum->conf_id    = rep->conf_id;
    quorum->group_uuid = rep->group_uuid;
    quorum->primary    = true;

    return 0;
}

