/*
 * Copyright (C) 2008-2019 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to state messages - implementation
 *
 */

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <string.h>
#include <galerautils.h>
#include <gu_serialize.hpp>
#include <gu_hexdump.h>

#define GCS_STATE_MSG_VER 6
#define GCS_STATE_MSG_NO_PROTO_DOWNGRADE_VER 6

#define GCS_STATE_MSG_ACCESS
#include "gcs_state_msg.hpp"
#include "gcs_node.hpp"

#include "gu_logger.hpp"

gcs_state_msg_t*
gcs_state_msg_create (const gu_uuid_t* state_uuid,
                      const gu_uuid_t* group_uuid,
                      const gu_uuid_t* prim_uuid,
                      gcs_seqno_t      prim_seqno,
                      gcs_seqno_t      received,
                      gcs_seqno_t      cached,
                      gcs_seqno_t      last_applied,
                      gcs_seqno_t      vote_seqno,
                      int64_t          vote_res,
                      uint8_t          vote_policy,
                      int              prim_joined,
                      gcs_node_state_t prim_state,
                      gcs_node_state_t current_state,
                      const char*      name,
                      const char*      inc_addr,
                      int              gcs_proto_ver, /* max supported versions*/
                      int              repl_proto_ver,
                      int              appl_proto_ver,
                      int              prim_gcs_ver, /* last prim versions*/
                      int              prim_repl_ver,
                      int              prim_appl_ver,
                      int              desync_count,
                      uint8_t          flags)
{
#define CHECK_PROTO_RANGE(LEVEL)                                        \
    if (LEVEL < (int)0 || LEVEL > (int)UINT8_MAX) {                     \
        gu_error(#LEVEL " value %d is out of range [0, %d]", LEVEL,UINT8_MAX); \
        return NULL;                                                    \
    }

    CHECK_PROTO_RANGE(gcs_proto_ver);
    CHECK_PROTO_RANGE(repl_proto_ver);
    CHECK_PROTO_RANGE(appl_proto_ver);
    CHECK_PROTO_RANGE(prim_gcs_ver);
    CHECK_PROTO_RANGE(prim_repl_ver);
    CHECK_PROTO_RANGE(prim_appl_ver);

    size_t name_len = strlen(name) + 1;
    size_t addr_len = strlen(inc_addr) + 1;
    gcs_state_msg_t* ret =
        static_cast<gcs_state_msg_t*>(
            gu_calloc (1, sizeof (gcs_state_msg_t) + name_len + addr_len));

    if (ret) {
        ret->state_uuid    = *state_uuid;
        ret->group_uuid    = *group_uuid;
        ret->prim_uuid     = *prim_uuid;
        ret->prim_joined   = prim_joined;
        ret->prim_seqno    = prim_seqno;
        ret->received      = received;
        ret->cached        = cached;
        ret->last_applied  = last_applied;
        ret->vote_seqno    = vote_seqno;
        ret->vote_res      = vote_res;
        ret->vote_policy   = vote_policy;
        ret->prim_state    = prim_state;
        ret->current_state = current_state;
        ret->version       = GCS_STATE_MSG_VER;
        ret->gcs_proto_ver = gcs_proto_ver;
        ret->repl_proto_ver= repl_proto_ver;
        ret->appl_proto_ver= appl_proto_ver;
        ret->prim_gcs_ver  = prim_gcs_ver;
        ret->prim_repl_ver = prim_repl_ver;
        ret->prim_appl_ver = prim_appl_ver;
        ret->desync_count  = desync_count;
        ret->name          = (char*)(ret + 1);
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
        sizeof (int8_t)      +   // gcs_proto_ver
        sizeof (int8_t)      +   // repl_proto_ver
        sizeof (int8_t)      +   // prim_state
        sizeof (int8_t)      +   // curr_state
        sizeof (int16_t)     +   // prim_joined
        sizeof (gu_uuid_t)   +   // state_uuid
        sizeof (gu_uuid_t)   +   // group_uuid
        sizeof (gu_uuid_t)   +   // conf_uuid
        sizeof (int64_t)     +   // received
        sizeof (int64_t)     +   // prim_seqno
        strlen (state->name) + 1 +
        strlen (state->inc_addr) + 1 +
// V1-2 stuff
        sizeof (uint8_t)     +   // appl_proto_ver (in preparation for V1)
// V3 stuff
        sizeof (int64_t)     +   // cached
// V4 stuff
        sizeof (int32_t)     +   // desync count
// V5 stuff
        sizeof (int64_t)     +   // last_applied
        sizeof (int64_t)     +   // vote_seqno
        sizeof (int64_t)     +   // vote_res
        sizeof (uint8_t)     +   // vote_policy
// V6 stuff
        sizeof (int8_t)      +   // prim_gcs_ver
        sizeof (int8_t)      +   // prim_repl_ver
        sizeof (int8_t)      +   // prim_appl_ver
        0
        );
}

#define STATE_MSG_FIELDS_V0(buf)                                 \
    int8_t*    version        = (int8_t*)buf;                    \
    int8_t*    flags          = version        + 1;              \
    int8_t*    gcs_proto_ver  = flags          + 1;              \
    int8_t*    repl_proto_ver = gcs_proto_ver  + 1;              \
    int8_t*    prim_state     = repl_proto_ver + 1;              \
    int8_t*    curr_state     = prim_state     + 1;              \
    int16_t*   prim_joined    = (int16_t*)(curr_state + 1);      \
    gu_uuid_t* state_uuid     = (gu_uuid_t*)(prim_joined + 1);   \
    gu_uuid_t* group_uuid     = state_uuid     + 1;              \
    gu_uuid_t* prim_uuid      = group_uuid     + 1;              \
    int64_t*   received       = (int64_t*)(prim_uuid + 1);       \
    int64_t*   prim_seqno     = received       + 1;              \
    char*      name           = (char*)(prim_seqno + 1);

#define CONST_STATE_MSG_FIELDS_V0(buf)                                  \
    const int8_t*    version        = (int8_t*)buf;                     \
    const int8_t*    flags          = version        + 1;               \
    const int8_t*    gcs_proto_ver  = flags          + 1;               \
    const int8_t*    repl_proto_ver = gcs_proto_ver  + 1;               \
    const int8_t*    prim_state     = repl_proto_ver + 1;               \
    const int8_t*    curr_state     = prim_state     + 1;               \
    const int16_t*   prim_joined    = (int16_t*)(curr_state + 1);       \
    const gu_uuid_t* state_uuid     = (gu_uuid_t*)(prim_joined + 1);    \
    const gu_uuid_t* group_uuid     = state_uuid     + 1;               \
    const gu_uuid_t* prim_uuid      = group_uuid     + 1;               \
    const int64_t*   received       = (int64_t*)(prim_uuid + 1);        \
    const int64_t*   prim_seqno     = received       + 1;               \
    const char*      name           = (char*)(prim_seqno + 1);


/* Serialize gcs_state_msg_t into buf */
ssize_t
gcs_state_msg_write (void* buf, const gcs_state_msg_t* state)
{
    STATE_MSG_FIELDS_V0(buf);
    char*     inc_addr  = name + strlen (state->name) + 1;
    uint8_t*  appl_proto_ver = (uint8_t*)(inc_addr + strlen(state->inc_addr) +1);
// V3 stuff
    int64_t*  cached         = (int64_t*)(appl_proto_ver + 1);
// V4 stuff
    int32_t*  desync_count   = (int32_t*)(cached + 1);
// V5 stuff
    int64_t* last_applied   = (int64_t*)(desync_count + 1);
    int64_t* vote_seqno     = last_applied + 1;
    int64_t* vote_res       = vote_seqno   + 1;
    uint8_t* vote_policy    = (uint8_t*)(vote_res + 1);
// V6 stuff
    uint8_t*  prim_gcs_ver   = (uint8_t*)(vote_policy + 1);
    uint8_t*  prim_repl_ver  = (uint8_t*)(prim_gcs_ver + 1);
    uint8_t*  prim_appl_ver  = (uint8_t*)(prim_repl_ver + 1);

    *version        = GCS_STATE_MSG_VER;
    *flags          = state->flags;
    *gcs_proto_ver  = state->gcs_proto_ver;
    *repl_proto_ver = state->repl_proto_ver;
    *prim_state     = state->prim_state;
    *curr_state     = state->current_state;
    *prim_joined    = htog16(((int16_t)state->prim_joined));
    *state_uuid     = state->state_uuid;
    *group_uuid     = state->group_uuid;
    *prim_uuid      = state->prim_uuid;
    *received       = htog64(state->received);
    *prim_seqno     = htog64(state->prim_seqno);

    /* from this point alignment breaks */
    strcpy (name,     state->name);
    strcpy (inc_addr, state->inc_addr);
    *appl_proto_ver = state->appl_proto_ver; // in preparation for V1

    gu::serialize8(state->cached, cached, 0);
    gu::serialize4(state->desync_count, desync_count, 0);
    gu::serialize8(state->last_applied, last_applied, 0);

    gu::serialize8(state->vote_seqno, vote_seqno, 0); // 4.ee
    gu::serialize8(state->vote_res, vote_res, 0);
    gu::serialize1(state->vote_policy, vote_policy, 0);

    *prim_gcs_ver    = state->prim_gcs_ver;
    *prim_repl_ver   = state->prim_repl_ver;
    *prim_appl_ver   = state->prim_appl_ver;

    size_t const msg_len((uint8_t*)(prim_appl_ver + 1) - (uint8_t*)buf);
#ifndef NDEBUG
    char str[1024];
    gu_hexdump(buf, msg_len, str, sizeof(str), true);
    gu_debug("Serialized state message of size %zd\n%s", msg_len, str);
#endif /* NDEBUG */
    return msg_len;
}

/* De-serialize gcs_state_msg_t from buf */
gcs_state_msg_t*
gcs_state_msg_read (const void* const buf, ssize_t const buf_len)
{
    assert (buf_len > 0);

#ifndef NDEBUG
    char str[1024];
    gu_hexdump(buf, buf_len, str, sizeof(str), true);
    gu_debug("Received state message of size %zd\n%s", buf_len, str);
#endif /* NDEBUG*/

    /* beginning of the message is always version 0 */
    CONST_STATE_MSG_FIELDS_V0(buf);
    const char* inc_addr = name + strlen (name) + 1;

    int      appl_proto_ver = 0;
    uint8_t* appl_ptr = (uint8_t*)(inc_addr + strlen(inc_addr) + 1);
    if (*version >= 1) {
        assert(buf_len >= (uint8_t*)(appl_ptr + 1) - (uint8_t*)buf);
        appl_proto_ver = *appl_ptr;
    }

    int64_t  cached = GCS_SEQNO_ILL;
    int64_t* cached_ptr = (int64_t*)(appl_ptr + 1);
    if (*version >= 3) {
        assert(buf_len >= (uint8_t*)(cached_ptr + 1) - (uint8_t*)buf);
        gu::unserialize8(cached_ptr, 0, cached);
    }
// v4 stuff
    int32_t  desync_count = 0;
    int32_t* desync_count_ptr = (int32_t*)(cached_ptr + 1);
    if (*version >= 4) {
        assert(buf_len >= (uint8_t*)(desync_count_ptr + 1) - (uint8_t*)buf);
        gu::unserialize4(desync_count_ptr, 0, desync_count);
    }
// v5 stuff
    int64_t last_applied = 0;
    int64_t vote_seqno   = 0;
    int64_t vote_res     = 0;
    uint8_t vote_policy  = GCS_VOTE_ZERO_WINS; // backward compatibility
    int64_t* last_applied_ptr = (int64_t*)(desync_count_ptr + 1);
    if (*version >= 5 && *gcs_proto_ver >= 2) {
        assert(buf_len > (uint8_t*)(last_applied_ptr + 3) - (uint8_t*)buf);
        gu::unserialize8(last_applied_ptr, 0, last_applied);

        gu::unserialize8(last_applied_ptr + 1, 0, vote_seqno);
        gu::unserialize8(last_applied_ptr + 2, 0, vote_res);
        gu::unserialize1(last_applied_ptr + 3, 0, vote_policy);
    }
// v6 stuff
    uint8_t prim_gcs_ver   = 0;
    uint8_t* prim_gcs_ptr  = (uint8_t*)(last_applied_ptr + 3) + 1;
    uint8_t prim_repl_ver  = 0;
    uint8_t* prim_repl_ptr = (uint8_t*)(prim_gcs_ptr + 1);
    uint8_t prim_appl_ver  = 0;
    uint8_t* prim_appl_ptr = (uint8_t*)(prim_repl_ptr + 1);
    if (*version >= 6) {
        assert(buf_len >= (uint8_t*)(prim_appl_ptr + 1) - (uint8_t*)buf);
        prim_gcs_ver    = *prim_gcs_ptr;
        prim_repl_ver   = *prim_repl_ptr;
        prim_appl_ver   = *prim_appl_ptr;
    }

    gcs_state_msg_t* ret = gcs_state_msg_create (
        state_uuid,
        group_uuid,
        prim_uuid,
        gtoh64(*prim_seqno),
        gtoh64(*received),
        cached,
        last_applied,
        vote_seqno,
        vote_res,
        vote_policy,
        gtoh16(*prim_joined),
        (gcs_node_state_t)*prim_state,
        (gcs_node_state_t)*curr_state,
        name,
        inc_addr,
        *gcs_proto_ver,
        *repl_proto_ver,
        appl_proto_ver,
        prim_gcs_ver,
        prim_repl_ver,
        prim_appl_ver,
        desync_count,
        *flags
        );

    if (ret) ret->version = *version; // dirty hack

    return ret;
}

/* Print state message contents to buffer */
int
gcs_state_msg_snprintf (char* str, size_t size, const gcs_state_msg_t* state)
{
    str[size - 1] = '\0'; // preventive termination
    return snprintf (str, size - 1,
                     "\n\tVersion      : %d"
                     "\n\tFlags        : %#02hhx"
                     "\n\tProtocols    : %d / %d / %d"
                     "\n\tState        : %s"
                     "\n\tDesync count : %d"
                     "\n\tPrim state   : %s"
                     "\n\tPrim UUID    : " GU_UUID_FORMAT
                     "\n\tPrim  seqno  : %lld"
                     "\n\tFirst seqno  : %lld"
                     "\n\tLast  seqno  : %lld"
                     "\n\tCommit cut   : %lld"
                     "\n\tLast vote    : %lld.%0llx"
                     "\n\tVote policy  : %d"
                     "\n\tPrim JOINED  : %d"
                     "\n\tState UUID   : " GU_UUID_FORMAT
                     "\n\tGroup UUID   : " GU_UUID_FORMAT
                     "\n\tName         : '%s'"
                     "\n\tIncoming addr: '%s'\n",
                     state->version,
                     state->flags,
                     state->gcs_proto_ver, state->repl_proto_ver,
                     state->appl_proto_ver,
                     gcs_node_state_to_str(state->current_state),
                     state->desync_count,
                     gcs_node_state_to_str(state->prim_state),
                     GU_UUID_ARGS(&state->prim_uuid),
                     (long long)state->prim_seqno,
                     (long long)state->cached,
                     (long long)state->received,
                     (long long)state->last_applied,
                     (long long)state->vote_seqno,(long long)state->vote_res,
                     state->vote_policy,
                     state->prim_joined,
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
gcs_state_msg_group_uuid (const gcs_state_msg_t* state)
{
    return &state->group_uuid;
}

/* Get action seqno */
gcs_seqno_t
gcs_state_msg_received (const gcs_state_msg_t* state)
{
    return state->received;
}

/* Get first cached action seqno */
gcs_seqno_t
gcs_state_msg_cached (const gcs_state_msg_t* state)
{
    return state->cached;
}

/* Get last applied action seqno */
gcs_seqno_t
gcs_state_msg_last_applied (const gcs_state_msg_t* state)
{
    return state->last_applied;
}

/* Get last applied action vote */
void
gcs_state_msg_last_vote (const gcs_state_msg_t* state,
                         gcs_seqno_t& seqno, int64_t& res)
{
    seqno = state->vote_seqno;
    res   = state->vote_res;
}

uint8_t
gcs_state_msg_vote_policy (const gcs_state_msg_t* state)
{
    return state->vote_policy;
}

/* Get current node state */
gcs_node_state_t
gcs_state_msg_current_state (const gcs_state_msg_t* state)
{
    return state->current_state;
}

/* Get node state */
gcs_node_state_t
gcs_state_msg_prim_state (const gcs_state_msg_t* state)
{
    return state->prim_state;
}

/* Get node name */
const char*
gcs_state_msg_name (const gcs_state_msg_t* state)
{
    return state->name;
}

/* Get node incoming address */
const char*
gcs_state_msg_inc_addr (const gcs_state_msg_t* state)
{
    return state->inc_addr;
}

/* Get supported protocols */
void
gcs_state_msg_get_proto_ver (const gcs_state_msg_t* state,
                             int* gcs_proto_ver,
                             int* repl_proto_ver,
                             int* appl_proto_ver)
{
    *gcs_proto_ver  = state->gcs_proto_ver;
    *repl_proto_ver = state->repl_proto_ver;
    *appl_proto_ver = state->appl_proto_ver;
}

int
gcs_state_msg_get_desync_count (const gcs_state_msg_t* state)
{
    return state->desync_count;
}

/* Get state message flags */
uint8_t
gcs_state_msg_flags (const gcs_state_msg_t* state)
{
    return state->flags;
}

/* Returns the node which is most representative of a group */
static const gcs_state_msg_t*
state_nodes_compare (const gcs_state_msg_t* left, const gcs_state_msg_t* right)
{
    assert (0 == gu_uuid_compare(&left->group_uuid, &right->group_uuid));
    /* Allow GCS_SEQNO_ILL seqnos if bootstrapping from non-prim */
    assert ((gcs_state_msg_flags(left)  & GCS_STATE_FBOOTSTRAP) ||
            left->prim_seqno  != GCS_SEQNO_ILL);
    assert ((gcs_state_msg_flags(right) & GCS_STATE_FBOOTSTRAP) ||
            right->prim_seqno != GCS_SEQNO_ILL);

    if (left->received < right->received) {
        assert (left->prim_seqno <= right->prim_seqno);
        return right;
    }
    else if (left->received > right->received) {
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
            int written = gcs_state_msg_snprintf (buf, buf_len, states[j]);
            buf     += written;
            buf_len -= written;
        }
    }
}

#define GCS_STATE_MAX_LEN 722

#define GCS_STATE_BAD_REP ((gcs_state_msg_t*)-1)

/*! checks for inherited primary configuration, returns representative
 * @retval (void*)-1 in case of fatal error */
static const gcs_state_msg_t*
state_quorum_inherit (const gcs_state_msg_t* states[],
                      size_t                 states_num,
                      gcs_state_quorum_t*    quorum)
{
    /* They all must have the same group_uuid or otherwise quorum is impossible.
     * Of those we need to find at least one that has complete state -
     * status >= GCS_STATE_JOINED. If we find none - configuration is
     * non-primary.
     * Of those with the status >= GCS_STATE_JOINED we choose the most
     * representative: with the highest act_seqno and prim_seqno.
     */
    size_t i, j;
    const gcs_state_msg_t* rep = NULL;

    // find at least one JOINED/DONOR (donor was once joined)
    for (i = 0; i < states_num; i++) {
        if (gcs_node_is_joined(states[i]->current_state)) {
            rep = states[i];
            break;
        }
    }

    if (!rep) {
        size_t buf_len = states_num * GCS_STATE_MAX_LEN;
        char*  buf = static_cast<char*>(gu_malloc (buf_len));

        if (buf) {
            state_report_uuids (buf, buf_len, states, states_num,
                                GCS_NODE_STATE_NON_PRIM);
#ifdef GCS_CORE_TESTING
            gu_warn ("Quorum: No node with complete state:\n%s", buf);
#else
            /* Print buf into stderr in order to message truncation
             * of application logger. */
            gu_warn ("Quorum: No node with complete state:");
            fprintf(stderr, "%s\n", buf);
#endif /* GCS_CORE_TESTING */
            gu_free (buf);
        }

        return NULL;
    }

    // Check that all JOINED/DONOR have the same group UUID
    // and find most updated
    for (j = i + 1; j < states_num; j++) {
        if (gcs_node_is_joined(states[j]->current_state)) {
            if (gu_uuid_compare (&rep->group_uuid, &states[j]->group_uuid)) {
                // for now just freak out and print all conflicting nodes
                size_t buf_len = states_num * GCS_STATE_MAX_LEN;
                char*  buf = static_cast<char*>(gu_malloc (buf_len));
                if (buf) {
                    state_report_uuids (buf, buf_len, states, states_num,
                                        GCS_NODE_STATE_DONOR);
                    gu_fatal("Quorum impossible: conflicting group UUIDs:\n%s");
                    gu_free (buf);
                }
                else {
                    gu_fatal("Quorum impossible: conflicting group UUIDs");
                }

                return GCS_STATE_BAD_REP;
            }
            rep = state_nodes_compare (rep, states[j]);
        }
    }

    quorum->act_id     = rep->received;
    quorum->conf_id    = rep->prim_seqno;
    quorum->last_applied = rep->last_applied;
    quorum->group_uuid = rep->group_uuid;
    quorum->primary    = true;

    return rep;
}

struct candidate /* remerge candidate */
{
    gu_uuid_t              prim_uuid;              // V0 compatibility (0.8.1)
    gu_uuid_t              state_uuid;
    gcs_seqno_t            state_seqno;
    const gcs_state_msg_t* rep;
    int                    prim_joined;
    int                    found;
    gcs_seqno_t            prim_seqno;
};

static bool
state_match_candidate (const gcs_state_msg_t* const s,
                       struct candidate*      const c,
                       int                    const state_exchange_version)
{
    switch (state_exchange_version)
    {
    case 0:                                       // V0 compatibility (0.8.1)
        return (0 == gu_uuid_compare(&s->prim_uuid, &c->prim_uuid));
    default:
        return ((0 == gu_uuid_compare(&s->group_uuid, &c->state_uuid)) &&
                (s->received == c->state_seqno) &&
                // what if they are different components.
                // but have same group uuid and received(0)
                // see gh24.
                (s->prim_seqno == c->prim_seqno));
    }
}

/* try to find representative remerge candidate */
static const struct candidate*
state_rep_candidate (const struct candidate* const c,
                     int                     const c_num)
{
    assert (c_num > 0);

    const struct candidate* rep = &c[0];
    gu_uuid_t const state_uuid  = rep->state_uuid;
    gcs_seqno_t     state_seqno = rep->state_seqno;
    gcs_seqno_t     prim_seqno  = rep->prim_seqno;
    int i;

    for (i = 1; i < c_num; i++) {
        if (!gu_uuid_compare(&c[i].state_uuid, &GU_UUID_NIL))
        {
            /* Ignore nodes with undefined state uuid, they have been
             * added to group before remerge and have clean state. */
            continue;
        }
        else if (gu_uuid_compare(&state_uuid, &GU_UUID_NIL) &&
                 gu_uuid_compare(&state_uuid, &c[i].state_uuid)) {
            /* There are candidates from different groups */
            return NULL;
        }

        assert (prim_seqno != c[i].prim_seqno ||
                state_seqno != c[i].state_seqno);

        if (prim_seqno < c[i].prim_seqno) {
            rep = &c[i];
            prim_seqno = rep->prim_seqno;
        } else if (prim_seqno == c[i].prim_seqno &&
                   state_seqno < c[i].state_seqno) {
            rep = &c[i];
            state_seqno = rep->state_seqno;
        }
    }

    return rep;
}

/*! checks for full prim remerge after non-prim */
static const gcs_state_msg_t*
state_quorum_remerge (const gcs_state_msg_t* const states[],
                      long                   const states_num,
                      bool                   const bootstrap,
                      gcs_state_quorum_t*    const quorum)
{
    struct candidate* candidates = GU_CALLOC(states_num, struct candidate);

    if (!candidates) {
        gu_error ("Quorum: could not allocate %zd bytes for re-merge check.",
                  states_num * sizeof(struct candidate));
        return NULL;
    }

    int i, j;
    int candidates_found = 0;

    /* 1. Sort and count all nodes who have ever been JOINED by primary
     *    component UUID */
    for (i = 0; i < states_num; i++) {
        bool cond;

        if (bootstrap) {
            cond = gcs_state_msg_flags(states[i]) & GCS_STATE_FBOOTSTRAP;
            if (cond) gu_debug("found node %s with bootstrap flag",
                               gcs_state_msg_name(states[i]));
        }
        else {
            cond = gcs_node_is_joined(states[i]->prim_state);
        }

        if (cond) {
            if (!bootstrap &&
                GCS_NODE_STATE_JOINER == states[i]->current_state) {
                /* Joiner always has an undefined state
                 * (and it should be its prim_state!) */
                gu_warn ("Inconsistent state message from %d (%s): current "
                         "state is %s, but the primary state was %s.",
                         i, states[i]->name,
                         gcs_node_state_to_str(states[i]->current_state),
                         gcs_node_state_to_str(states[i]->prim_state));
                continue;
            }

            assert(bootstrap ||
                   gu_uuid_compare(&states[i]->prim_uuid, &GU_UUID_NIL));

            for (j = 0; j < candidates_found; j++) {
                if (state_match_candidate (states[i], &candidates[j],
                                           quorum->version)) {
                    assert(states[i]->prim_joined == candidates[j].prim_joined);
                    // comment out following two lines for pc recovery
                    // when nodes recoveried from state files, if their states
                    // match, so candidates[j].found > 0.
                    // However their prim_joined == 0.
                    // assert(candidates[j].found < candidates[j].prim_joined);
                    // assert(candidates[j].found > 0);

                    candidates[j].found++;

                    candidates[j].rep =
                        state_nodes_compare (candidates[j].rep, states[i]);

                    break;
                }
            }

            if (j == candidates_found) {
                // we don't have this candidate in the list yet
                candidates[j].prim_uuid   = states[i]->prim_uuid;
                candidates[j].state_uuid  = states[i]->group_uuid;
                candidates[j].state_seqno = states[i]->received;
                candidates[j].prim_joined = states[i]->prim_joined;
                candidates[j].rep         = states[i];
                candidates[j].found       = 1;
                candidates[j].prim_seqno  = states[i]->prim_seqno;
                candidates_found++;

                assert(candidates_found <= states_num);
            }
        }
    }


    const gcs_state_msg_t* rep = NULL;

    if (candidates_found) {
        assert (candidates_found > 0);

        const struct candidate* const rc =
            state_rep_candidate (candidates, candidates_found);

        if (!rc) {
            gu_error ("Found more than one %s primary component candidate.",
                      bootstrap ? "bootstrap" : "re-merged");
            rep = NULL;
        }
        else {
            if (bootstrap) {
                gu_info ("Bootstrapped primary " GU_UUID_FORMAT " found: %d.",
                         GU_UUID_ARGS(&rc->prim_uuid), rc->found);
            }
            else {
                gu_info ("%s re-merge of primary " GU_UUID_FORMAT " found: "
                         "%d of %d.",
                         rc->found == rc->prim_joined ? "Full" : "Partial",
                         GU_UUID_ARGS(&rc->prim_uuid),
                         rc->found, rc->prim_joined);
            }

            rep = rc->rep;
            assert (NULL != rep);
            assert (bootstrap || gcs_node_is_joined(rep->prim_state));

            quorum->act_id     = rep->received;
            quorum->conf_id    = rep->prim_seqno;
            quorum->last_applied = rep->last_applied;
            quorum->group_uuid = rep->group_uuid;
            quorum->primary    = true;
        }
    }
    else {
        assert (0 == candidates_found);
        gu_warn ("No %s primary component found.",
                 bootstrap ? "bootstrapped" : "re-merged");
    }

    gu_free (candidates);

    return rep;
}

#if 0 // REMOVE WHEN NO LONGER NEEDED FOR REFERENCE
/*! Checks for prim comp bootstrap */
static const gcs_state_msg_t*
state_quorum_bootstrap (const gcs_state_msg_t* const states[],
                        long                   const states_num,
                        gcs_state_quorum_t*    const quorum)
{
    struct candidate* candidates = GU_CALLOC(states_num, struct candidate);

    if (!candidates) {
        gu_error ("Quorum: could not allocate %zd bytes for re-merge check.",
                  states_num * sizeof(struct candidate));
        return NULL;
    }

    int i, j;
    int candidates_found = 0;

    /* 1. Sort and count all nodes which have bootstrap flag set */
    for (i = 0; i < states_num; i++) {
        if (gcs_state_msg_flags(states[i]) & GCS_STATE_FBOOTSTRAP) {
            gu_debug("found node %s with bootstrap flag",
                     gcs_state_msg_name(states[i]));
            for (j = 0; j < candidates_found; j++) {
                if (state_match_candidate (states[i], &candidates[j],
                                           quorum->version)) {
                    assert(states[i]->prim_joined == candidates[j].prim_joined);
                    assert(candidates[j].found > 0);

                    candidates[j].found++;

                    candidates[j].rep =
                        state_nodes_compare (candidates[j].rep, states[i]);

                    break;
                }
            }

            if (j == candidates_found) {
                // we don't have this candidate in the list yet
                candidates[j].prim_uuid   = states[i]->prim_uuid;
                candidates[j].state_uuid  = states[i]->group_uuid;
                candidates[j].state_seqno = states[i]->received;
                candidates[j].prim_joined = states[i]->prim_joined;
                candidates[j].rep         = states[i];
                candidates[j].found       = 1;
                candidates_found++;

                assert(candidates_found <= states_num);
            }
        }
    }

    const gcs_state_msg_t* rep = NULL;

    if (candidates_found) {
        assert (candidates_found > 0);

        const struct candidate* const rc =
            state_rep_candidate (candidates, candidates_found);

        if (!rc) {
            gu_error ("Found more than one bootstrap primary component "
                      "candidate.");
            rep = NULL;
        }
        else {
            gu_info ("Bootstrapped primary " GU_UUID_FORMAT " found: %d.",
                     GU_UUID_ARGS(&rc->prim_uuid), rc->found);

            rep = rc->rep;
            assert (NULL != rep);

            quorum->act_id     = rep->received;
            quorum->conf_id    = rep->prim_seqno;
            quorum->last_applied = rep->last_applied;
            quorum->group_uuid = rep->group_uuid;
            quorum->primary    = true;
        }
    }
    else {
        assert (0 == candidates_found);
        gu_warn ("No bootstrapped primary component found.");
    }

    gu_free (candidates);

    return rep;
}
#endif // 0

/* Get quorum decision from state messages */
long
gcs_state_msg_get_quorum (const gcs_state_msg_t* states[],
                          size_t                 states_num,
                          gcs_state_quorum_t*    quorum)
{
    assert (states_num > 0);
    assert (NULL != states);

    size_t i;
    const gcs_state_msg_t*   rep = NULL;

    *quorum = GCS_QUORUM_NON_PRIMARY; // pessimistic assumption

    /* find lowest commonly supported state exchange version */
    quorum->version = states[0]->version;
    for (i = 1; i < states_num; i++)
    {
        if (quorum->version > states[i]->version) {
            quorum->version = states[i]->version;
        }
    }

    rep = state_quorum_inherit (states, states_num, quorum);

    if (!quorum->primary && rep != GCS_STATE_BAD_REP) {
        rep = state_quorum_remerge (states, states_num, false, quorum);
    }

    if (!quorum->primary && rep != GCS_STATE_BAD_REP) {
        rep = state_quorum_remerge (states, states_num, true, quorum);
    }

    if (!quorum->primary) {
        gu_error ("Failed to establish quorum.");
        return 0;
    }

    assert (rep != NULL);

    // select the highest commonly supported protocol: min(proto_max)
#define INIT_PROTO_VER(LEVEL) quorum->LEVEL = rep->LEVEL
    INIT_PROTO_VER(gcs_proto_ver);
    INIT_PROTO_VER(repl_proto_ver);
    INIT_PROTO_VER(appl_proto_ver);
#undef INIT_PROTO_VER

    for (i = 0; i < states_num; i++)
    {
#define CHECK_MIN_PROTO_VER(LEVEL)                              \
        if (states[i]->LEVEL <  quorum->LEVEL) {                \
            quorum->LEVEL = states[i]->LEVEL;                   \
        }

//        if (!gu_uuid_compare(&states[i]->group_uuid, &quorum->group_uuid)) {
            CHECK_MIN_PROTO_VER(gcs_proto_ver);
            CHECK_MIN_PROTO_VER(repl_proto_ver);
            CHECK_MIN_PROTO_VER(appl_proto_ver);
//        }
#undef CHECK_MIN_PROTO_VER
    }

    if (quorum->version >= GCS_STATE_MSG_NO_PROTO_DOWNGRADE_VER)
    {
        // forbid protocol downgrade
#define CHECK_MIN_PROTO_VER(LEVEL)                                      \
        if (quorum->LEVEL##_proto_ver < rep->prim_##LEVEL##_ver) {      \
            quorum->LEVEL##_proto_ver = rep->prim_##LEVEL##_ver;        \
        }
        CHECK_MIN_PROTO_VER(gcs);
        CHECK_MIN_PROTO_VER(repl);
        CHECK_MIN_PROTO_VER(appl);
#undef CHECK_MIN_PROTO_VER
    }

    if (quorum->gcs_proto_ver < 1)
    {
        quorum->vote_policy = GCS_VOTE_ZERO_WINS;
    }
    else
    {
        quorum->vote_policy = rep->vote_policy;
    }

    if (quorum->version < 2) {;} // for future generations

    if (quorum->version < 1) {
        // appl_proto_ver is not supported by all members
        assert (quorum->repl_proto_ver <= 1);
        if (1 == quorum->repl_proto_ver)
            quorum->appl_proto_ver = 1;
        else
            quorum->appl_proto_ver = 0;
    }

    return 0;
}
