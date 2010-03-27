/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to state messages - implementation
 *
 */

#include <string.h>
#include <galerautils.h>

#define GCS_STATE_ACCESS
#include "gcs_state.h"

gcs_state_msg_t*
gcs_state_msg_create (const gu_uuid_t* state_uuid,
                      const gu_uuid_t* group_uuid,
                      const gu_uuid_t* prim_uuid,
                      long             prim_joined,
                      gcs_seqno_t      prim_seqno,
                      gcs_seqno_t      act_seqno,
                      gcs_node_state_t prim_state,
                      gcs_node_state_t current_state,
                      const char*      name,
                      const char*      inc_addr,
                      gcs_proto_t      proto_min,
                      gcs_proto_t      proto_max,
                      uint8_t          flags)
{
    size_t name_len  = strlen(name) + 1;
    size_t addr_len  = strlen(inc_addr) + 1;
    gcs_state_msg_t* ret =
        gu_calloc (1, sizeof (gcs_state_msg_t) + name_len + addr_len);

    if (ret) {
        ret->state_uuid    = *state_uuid;
        ret->group_uuid    = *group_uuid;
        ret->prim_uuid     = *prim_uuid;
        ret->prim_joined   = prim_joined;
        ret->prim_seqno    = prim_seqno;
        ret->act_seqno     = act_seqno;
        ret->prim_state    = prim_state;
        ret->current_state = current_state;
        ret->proto_min     = proto_min;
        ret->proto_max     = proto_max;
        ret->name          = (void*)(ret) + sizeof (gcs_state_msg_t);
        ret->inc_addr      = ret->name + name_len;
        ret->flags         = flags;

        // tmp is a workaround for some combination of GCC flags which don't
        // allow passing ret->name and ret->inc_addr directly even with casting
        // char* tmp = (char*)ret->name;
        strcpy ((char*)ret->name, name);
        // tmp = (char*)ret->inc_addr;
        strcpy ((char*)ret->inc_addr, inc_addr);
    }

    return ret;
}

void
gcs_state_msg_destroy (gcs_state_msg_t* state)
{
    gu_free (state);
}

/* Returns length needed to serialize gcs_state_msg_t for sending */
size_t
gcs_state_msg_len (gcs_state_msg_t* state)
{
    return (
        sizeof (int8_t)      +   // version (reserved)
        sizeof (int8_t)      +   // flags
        sizeof (int8_t)      +   // proto_min
        sizeof (int8_t)      +   // proto_max
        sizeof (int8_t)      +   // prim_state
        sizeof (int8_t)      +   // curr_state
        sizeof (int16_t)     +   // prim_joined
        sizeof (gu_uuid_t)   +   // state_uuid
        sizeof (gu_uuid_t)   +   // group_uuid
        sizeof (gu_uuid_t)   +   // conf_uuid
        sizeof (int64_t)     +   // act_seqno
        sizeof (int64_t)     +   // prim_seqno
        strlen (state->name) + 1 +
        strlen (state->inc_addr) + 1
        );
}

#define STATE_MSG_FIELDS_V0(_const,buf)                            \
    _const int8_t*    version     = (buf);                         \
    _const int8_t*    flags       = version    + 1;                \
    _const int8_t*    proto_min   = flags      + 1;                \
    _const int8_t*    proto_max   = proto_min  + 1;                \
    _const int8_t*    prim_state  = proto_max  + 1;                \
    _const int8_t*    curr_state  = prim_state + 1;                \
    _const int16_t*   prim_joined = (int16_t*)(curr_state + 1);    \
    _const gu_uuid_t* state_uuid  = (gu_uuid_t*)(prim_joined + 1); \
    _const gu_uuid_t* group_uuid  = state_uuid + 1;                \
    _const gu_uuid_t* prim_uuid   = group_uuid + 1;                \
    _const int64_t*   act_seqno   = (int64_t*)(prim_uuid + 1);     \
    _const int64_t*   prim_seqno  = act_seqno + 1;                 \
    _const char*      name        = (char*)(prim_seqno + 1);

/* Serialize gcs_state_msg_t into buf */
ssize_t
gcs_state_msg_write (void* buf, const gcs_state_msg_t* state)
{
    STATE_MSG_FIELDS_V0(,buf);
    char*     inc_addr  = name + strlen (state->name) + 1;

    *version     = 0;
    *flags       = state->flags;
    *proto_min   = state->proto_min;
    *proto_max   = state->proto_max;
    *prim_state  = state->prim_state;
    *curr_state  = state->current_state;
    *prim_joined = gu_le16(((int16_t)state->prim_joined));
    *state_uuid  = state->state_uuid;
    *group_uuid  = state->group_uuid;
    *prim_uuid   = state->prim_uuid;
    *act_seqno   = gu_le64(state->act_seqno);
    *prim_seqno  = gu_le64(state->prim_seqno);
    strcpy (name,     state->name);
    strcpy (inc_addr, state->inc_addr);

    return (inc_addr + strlen(inc_addr) + 1 - (char*)buf);
}

/* De-serialize gcs_state_msg_t from buf */
gcs_state_msg_t*
gcs_state_msg_read (const void* buf, size_t buf_len)
{
    unsigned char version = *((uint8_t*)buf);

    switch (version) {
    case 0: {
        STATE_MSG_FIELDS_V0(const,buf);
        const char* inc_addr = name + strlen (name) + 1;

        return gcs_state_msg_create (
            state_uuid,
            group_uuid,
            prim_uuid,
            gu_le16(*prim_joined),
            gu_le64(*prim_seqno),
            gu_le64(*act_seqno),
            *prim_state,
            *curr_state,
            name,
            inc_addr,
            *proto_min,
            *proto_max,
            *flags
            );
    }
    default:
        gu_error ("Unrecognized state message v. %u", version);
        return NULL;
    }
}

/* Print state message contents to buffer */
int
gcs_state_snprintf (char* str, size_t size, const gcs_state_msg_t* state)
{
    str[size - 1] = '\0'; // preventive termination
    return snprintf (str, size - 1,
                     "\n\tFlags        : %u"
                     "\n\tProtocols    : %u - %u"
                     "\n\tState        : %s"
                     "\n\tPrim state   : %s"
                     "\n\tPrim UUID    : "GU_UUID_FORMAT
                     "\n\tPrim JOINED  : %ld"
                     "\n\tPrim seqno   : %lld"
                     "\n\tGlobal seqno : %lld"
                     "\n\tState UUID   : "GU_UUID_FORMAT
                     "\n\tGroup UUID   : "GU_UUID_FORMAT
                     "\n\tName         : '%s'"
                     "\n\tIncoming addr: '%s'\n",
                     state->flags,
                     state->proto_min, state->proto_max,
                     gcs_node_state_to_str(state->current_state),
                     gcs_node_state_to_str(state->prim_state),
                     GU_UUID_ARGS(&state->prim_uuid),
                     state->prim_joined,
                     (long long)state->prim_seqno,
                     (long long)state->act_seqno,
                     GU_UUID_ARGS(&state->state_uuid),
                     GU_UUID_ARGS(&state->group_uuid),
                     state->name,
                     state->inc_addr
        );
}

/* Get state uuid */
const gu_uuid_t*
gcs_state_msg_uuid (const gcs_state_msg_t* state)
{
    return &state->state_uuid;
}

/* Get group uuid */
const gu_uuid_t*
gcs_state_group_uuid (const gcs_state_msg_t* state)
{
    return &state->group_uuid;
}

/* Get action seqno */
gcs_seqno_t
gcs_state_act_id (const gcs_state_msg_t* state)
{
    return state->act_seqno;
}

/* Get current node state */
gcs_node_state_t
gcs_state_current_state (const gcs_state_msg_t* state)
{
    return state->current_state;
}

/* Get node state */
gcs_node_state_t
gcs_state_prim_state (const gcs_state_msg_t* state)
{
    return state->prim_state;
}

/* Get node name */
const char*
gcs_state_name (const gcs_state_msg_t* state)
{
    return state->name;
}

/* Get node incoming address */
const char*
gcs_state_inc_addr (const gcs_state_msg_t* state)
{
    return state->inc_addr;
}

/* Get supported protocols */
gcs_proto_t
gcs_state_proto_min (const gcs_state_msg_t* state)
{
    return state->proto_min;
}

gcs_proto_t
gcs_state_proto_max (const gcs_state_msg_t* state)
{
    return state->proto_max;
}

/* Returns the node which is most representative of a group */
static const gcs_state_msg_t*
state_nodes_compare (const gcs_state_msg_t* left, const gcs_state_msg_t* right)
{
    assert (0 == gu_uuid_compare(&left->group_uuid, &right->group_uuid));
    assert (left->prim_seqno  != GCS_SEQNO_ILL);
    assert (right->prim_seqno != GCS_SEQNO_ILL);

    if (left->act_seqno < right->act_seqno) {
        assert (left->prim_seqno <= right->prim_seqno);
        return right;
    }
    else if (left->act_seqno > right->act_seqno) {
        assert (left->prim_seqno >= right->prim_seqno);
        return left;
    }
    else {
        // act_id's are equal, choose the one with higher prim_seqno.
        if (left->prim_seqno < right->prim_seqno) {
            return right;
        }
        else {
            return left;
        }
    }
}

/* Helper - just prints out all significant (JOINED) nodes */
static void
state_report_uuids (char* buf, size_t buf_len,
                    const gcs_state_msg_t* states[], long states_num,
                    gcs_node_state_t min_state)
{
    long j;

    for (j = 0; j < states_num; j++) {
        if (states[j]->current_state >= min_state) {
            int written = gcs_state_snprintf (buf, buf_len, states[j]);
            buf     += written;
            buf_len -= written;
        }        
    }
}

#define GCS_STATE_MAX_LEN 720

/* Get quorum decision from state messages */
long 
gcs_state_get_quorum (const gcs_state_msg_t* states[],
                      long                   states_num,
                      gcs_state_quorum_t*    quorum)
{
    /* We count only nodes which come from primary configuraton -
     * prim_seqno != GCS_SEQNO_ILL
     * They all must have the same group_uuid or otherwise quorum is impossible.
     * Of those we need to find at least one that has complete state - 
     * status >= GCS_STATE_JOINED. If we find none - configuration is
     * non-primary.
     * Of those with the status >= GCS_STATE_JOINED we choose the most
     * representative: with the highest act_seqno and prim_seqno.
     */

    long i, j;
    const gcs_state_msg_t* rep = NULL;
    gcs_state_quorum_t GCS_STATE_QUORUM_NON_PRIMARY =
        {
            GU_UUID_NIL,
            GCS_SEQNO_ILL,
            GCS_SEQNO_ILL,
            false,
            -1
        };

    *quorum = GCS_STATE_QUORUM_NON_PRIMARY; // pessimistic assumption

    // find at least one JOINED/DONOR (donor was once joined)
    for (i = 0; i < states_num; i++) {
        if (states[i]->current_state >= GCS_NODE_STATE_DONOR) {
            rep = states[i];
            break;
        }
    }

    if (!rep) {
        size_t buf_len = states_num * GCS_STATE_MAX_LEN;
        char*  buf = gu_malloc (buf_len);
        if (buf) {
            state_report_uuids (buf, buf_len, states, states_num,
                                GCS_NODE_STATE_NON_PRIM);
            gu_warn ("Quorum impossible: No node with complete state:\n%s",
                     buf);
            gu_free (buf);
        }
        return 0;
    }

    // Check that all JOINED/DONOR have the same group UUID
    // and find most updated
    for (j = i+1; j < states_num; j++) {
        if (states[j]->current_state >= GCS_NODE_STATE_DONOR) {
            if (gu_uuid_compare (&rep->group_uuid, &states[i]->group_uuid)) {
                // for now just freak out and print all conflicting nodes
                size_t buf_len = states_num * GCS_STATE_MAX_LEN;
                char*  buf = gu_malloc (buf_len);
                if (buf) {
                    state_report_uuids (buf, buf_len, states, states_num,
                                        GCS_NODE_STATE_DONOR);
                    gu_fatal("Quorum impossible: conflicting group UUIDs:\n%s");
                    gu_free (buf);
                }
                return 0;
            }
            rep = state_nodes_compare (rep, states[i]);
        }
    }

    // 1. select the highest commonly supported protocol: min(proto_max)
    quorum->proto = rep->proto_max;
    for (i = 0; i < states_num; i++) {
        if (states[i]->proto_max <  quorum->proto &&
            states[i]->proto_max >= rep->proto_min) {
            quorum->proto = states[i]->proto_max;
        }
    }

    quorum->act_id     = rep->act_seqno;
    quorum->conf_id    = rep->prim_seqno;
    quorum->group_uuid = rep->group_uuid;
    quorum->primary    = true;

    return 0;
}

