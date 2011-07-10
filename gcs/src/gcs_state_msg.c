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

#define GCS_STATE_MSG_VER 0

#define GCS_STATE_MSG_ACCESS
#include "gcs_state_msg.h"
#include "gcs_node.h"

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
                      int              gcs_proto_ver,
                      int              repl_proto_ver,
                      int              appl_proto_ver,
                      uint8_t          flags)
{
#define CHECK_PROTO_RANGE(LEVEL)                                        \
    if (LEVEL < 0 || LEVEL > UINT8_MAX) {                               \
        gu_error ("#LEVEL value %d is out of range [0, %d]", LEVEL,UINT8_MAX); \
        return NULL;                                                       \
    }

    CHECK_PROTO_RANGE(gcs_proto_ver);
    CHECK_PROTO_RANGE(repl_proto_ver);
    CHECK_PROTO_RANGE(appl_proto_ver);

    size_t name_len = strlen(name) + 1;
    size_t addr_len = strlen(inc_addr) + 1;
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
        ret->version       = GCS_STATE_MSG_VER;
        ret->gcs_proto_ver = gcs_proto_ver;
        ret->repl_proto_ver= repl_proto_ver;
        ret->appl_proto_ver= appl_proto_ver;
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
        sizeof (int8_t)      +   // gcs_proto_ver
        sizeof (int8_t)      +   // repl_proto_ver
        sizeof (int8_t)      +   // prim_state
        sizeof (int8_t)      +   // curr_state
        sizeof (int16_t)     +   // prim_joined
        sizeof (gu_uuid_t)   +   // state_uuid
        sizeof (gu_uuid_t)   +   // group_uuid
        sizeof (gu_uuid_t)   +   // conf_uuid
        sizeof (int64_t)     +   // act_seqno
        sizeof (int64_t)     +   // prim_seqno
        strlen (state->name) + 1 +
        strlen (state->inc_addr) + 1 +
        sizeof (uint8_t)         // appl_proto_ver (in preparation for V1)
        );
}

#define STATE_MSG_FIELDS_V0(_const,buf)                                 \
    _const int8_t*    version        = (buf);                           \
    _const int8_t*    flags          = version        + 1;              \
    _const int8_t*    gcs_proto_ver  = flags          + 1;              \
    _const int8_t*    repl_proto_ver = gcs_proto_ver  + 1;              \
    _const int8_t*    prim_state     = repl_proto_ver + 1;              \
    _const int8_t*    curr_state     = prim_state     + 1;              \
    _const int16_t*   prim_joined    = (int16_t*)(curr_state + 1);      \
    _const gu_uuid_t* state_uuid     = (gu_uuid_t*)(prim_joined + 1);   \
    _const gu_uuid_t* group_uuid     = state_uuid     + 1;              \
    _const gu_uuid_t* prim_uuid      = group_uuid     + 1;              \
    _const int64_t*   act_seqno      = (int64_t*)(prim_uuid + 1);       \
    _const int64_t*   prim_seqno     = act_seqno      + 1;              \
    _const char*      name           = (char*)(prim_seqno + 1);

/* Serialize gcs_state_msg_t into buf */
ssize_t
gcs_state_msg_write (void* buf, const gcs_state_msg_t* state)
{
    STATE_MSG_FIELDS_V0(,buf);
    char*     inc_addr  = name + strlen (state->name) + 1;
    uint8_t*  appl_proto_ver = (void*)(inc_addr + strlen(state->inc_addr) + 1);

    *version        = GCS_STATE_MSG_VER;
    *flags          = state->flags;
    *gcs_proto_ver  = state->gcs_proto_ver;
    *repl_proto_ver = state->repl_proto_ver;
    *prim_state     = state->prim_state;
    *curr_state     = state->current_state;
    *prim_joined    = gu_le16(((int16_t)state->prim_joined));
    *state_uuid     = state->state_uuid;
    *group_uuid     = state->group_uuid;
    *prim_uuid      = state->prim_uuid;
    *act_seqno      = gu_le64(state->act_seqno);
    *prim_seqno     = gu_le64(state->prim_seqno);
    strcpy (name,     state->name);
    strcpy (inc_addr, state->inc_addr);
    *appl_proto_ver = state->appl_proto_ver; // in preparation for V1

    return (appl_proto_ver + 1 - (uint8_t*)buf);
}

/* De-serialize gcs_state_msg_t from buf */
gcs_state_msg_t*
gcs_state_msg_read (const void* buf, size_t buf_len)
{
    /* beginning of the message is always version 0 */
    STATE_MSG_FIELDS_V0(const,buf);
    const char* inc_addr = name + strlen (name) + 1;

    int appl_proto_ver = 0;
    if (*version >= 1) {
        appl_proto_ver = *(uint8_t*)(inc_addr + strlen(inc_addr) + 1);
    }

    gcs_state_msg_t* ret = gcs_state_msg_create (
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
        *gcs_proto_ver,
        *repl_proto_ver,
        appl_proto_ver,
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
                     "\n\tFlags        : %u"
                     "\n\tProtocols    : %d / %d / %d"
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
                     state->version,
                     state->flags,
                     state->gcs_proto_ver, state->repl_proto_ver,
                     state->appl_proto_ver,
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
gcs_state_msg_group_uuid (const gcs_state_msg_t* state)
{
    return &state->group_uuid;
}

/* Get action seqno */
gcs_seqno_t
gcs_state_msg_act_id (const gcs_state_msg_t* state)
{
    return state->act_seqno;
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
            int written = gcs_state_msg_snprintf (buf, buf_len, states[j]);
            buf     += written;
            buf_len -= written;
        }        
    }
}

#define GCS_STATE_MAX_LEN 722

/*! checks for inherited primary configuration, returns representative */
static const gcs_state_msg_t*
state_quorum_inherit (const gcs_state_msg_t* states[],
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

    // find at least one JOINED/DONOR (donor was once joined)
    for (i = 0; i < states_num; i++) {
        if (gcs_node_is_joined(states[i]->current_state)) {
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
            gu_warn ("Quorum: No node with complete state:\n%s",
                     buf);
            gu_free (buf);
        }

        return NULL;
    }

    // Check that all JOINED/DONOR have the same group UUID
    // and find most updated
    for (j = i+1; j < states_num; j++) {
        if (gcs_node_is_joined(states[j]->current_state)) {
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

                return NULL;
            }
            rep = state_nodes_compare (rep, states[i]);
        }
    }

    quorum->act_id     = rep->act_seqno;
    quorum->conf_id    = rep->prim_seqno;
    quorum->group_uuid = rep->group_uuid;
    quorum->primary    = true;

    return rep;
}

/*! checks for full prim remerge after non-prim */
static const gcs_state_msg_t*
state_quorum_remerge (const gcs_state_msg_t* states[],
                      long                   states_num,
                      gcs_state_quorum_t*    quorum)
{
    struct candidate /* merge candidate */
    {
        gu_uuid_t              prim_uuid;
        const gcs_state_msg_t* rep;
        int                    prim_joined;
        int                    found;
        int                    ver; /* compatibility with 0.8.0, #486 */
    };

    struct candidate* candidates = GU_CALLOC(states_num, struct candidate);

    if (!candidates) {
        gu_error ("Quorum: could not allocate %zd bytes for re-merge check.",
                  states_num * sizeof(struct candidate));
        return NULL;
    }

    int i, j;
    int candidates_found = 0;
    int merge_cnt        = 0;
    int merged           = 0; /* compatibility with 0.8.0 */

    /* 1. Sort and count all nodes who have ever been JOINED by primary
     *    component UUID */
    for (i = 0; i < states_num; i++) {
        if (gcs_node_is_joined(states[i]->prim_state)) {
            assert(gu_uuid_compare(&states[i]->prim_uuid, &GU_UUID_NIL));

            for (j = 0; j < candidates_found; j++) {
                if (0 == gu_uuid_compare(&states[i]->prim_uuid,
                                         &candidates[j].prim_uuid)) {
                    assert(states[i]->prim_joined == candidates[j].prim_joined);
                    assert(candidates[j].found < candidates[j].prim_joined);
                    assert(candidates[j].found > 0);

                    candidates[j].found++;

                    candidates[j].rep =
                        state_nodes_compare (candidates[j].rep, states[i]);

                    /* compatibility with 0.8.0, #486 */
                    candidates[j].ver =
                        candidates[j].ver <= states[i]->repl_proto_ver ?
                        candidates[j].ver :  states[i]->repl_proto_ver;

                    break;
                }
            }

            if (j == candidates_found) {
                // we don't have this primary UUID in the list yet
                candidates[j].prim_uuid   = states[i]->prim_uuid;
                candidates[j].prim_joined = states[i]->prim_joined;
                candidates[j].rep         = states[i];
                /* compatibility with 0.8.0, #486 */
                candidates[j].ver         = states[i]->repl_proto_ver;
                candidates[j].found       = 1;
                candidates_found++;

                assert(candidates_found <= states_num);
            }
// compatibility with 0.8.0, #486
            if (candidates[j].prim_joined == candidates[j].found) {
                gu_info ("Complete merge of primary "GU_UUID_FORMAT
                         " found: %ld of %ld.",
                         GU_UUID_ARGS(&candidates[j].prim_uuid),
                         candidates[j].found, candidates[j].prim_joined);
                merge_cnt++;
                merged = j;
//                // will be used only if merge_count == 1
//                rep = candidates[j].rep;
            }
// #endif compat 0.8.0
        }
    }

    const gcs_state_msg_t* rep = NULL;

    if (1 == candidates_found) {
if (candidates[0].ver == 0) { /* compatibility with 0.8.0 */
    if (0 == merge_cnt) {
        gu_warn ("No fully re-merged primary component found.");
        goto compatibility_080;
    }
}
else {
    merged = 0;
        gu_info ("%s re-merge of primary "GU_UUID_FORMAT" found: %ld of %ld.",
                 candidates[0].found == candidates[0].prim_joined ?
                 "Full" : "Partial",
                 GU_UUID_ARGS(&candidates[0].prim_uuid),
                 candidates[0].found, candidates[0].prim_joined);
}
        rep = candidates[merged].rep;
        assert (NULL != rep);
        assert (gcs_node_is_joined(rep->prim_state));

        quorum->act_id     = rep->act_seqno;
        quorum->conf_id    = rep->prim_seqno;
        quorum->group_uuid = rep->group_uuid;
        quorum->primary    = true;
    }
    else if (0 == candidates_found) {
        gu_warn ("No re-merged primary component found.");
    }
    else {
        assert (candidates_found > 1);
        gu_error ("Found more than one re-merged primary component candidate.");
        rep = NULL;
    }
compatibility_080:
    gu_free (candidates);

    return rep;
}

/* Get quorum decision from state messages */
long 
gcs_state_msg_get_quorum (const gcs_state_msg_t* states[],
                          long                   states_num,
                          gcs_state_quorum_t*    quorum)
{
    long i;
    const gcs_state_msg_t*   rep = NULL;

    *quorum = GCS_QUORUM_NON_PRIMARY; // pessimistic assumption

    rep = state_quorum_inherit (states, states_num, quorum);

    if (!quorum->primary) {
        rep = state_quorum_remerge (states, states_num, quorum);

        if (!quorum->primary) {
            gu_error ("Failed to establish quorum.");
            return 0;
        }
    }

    assert (rep != NULL);

    // select the highest commonly supported protocol: min(proto_max)
#define INIT_PROTO_VER(LEVEL) quorum->LEVEL = rep->LEVEL
    INIT_PROTO_VER(version);
    INIT_PROTO_VER(gcs_proto_ver);
    INIT_PROTO_VER(repl_proto_ver);
    INIT_PROTO_VER(appl_proto_ver);

    for (i = 0; i < states_num; i++) {

#define CHECK_MIN_PROTO_VER(LEVEL)                              \
        if (states[i]->LEVEL <  quorum->LEVEL) {                \
            quorum->LEVEL = states[i]->LEVEL;                   \
        }

        CHECK_MIN_PROTO_VER(version);

//        if (!gu_uuid_compare(&states[i]->group_uuid, &quorum->group_uuid)) {
            CHECK_MIN_PROTO_VER(gcs_proto_ver);
            CHECK_MIN_PROTO_VER(repl_proto_ver);
            CHECK_MIN_PROTO_VER(appl_proto_ver);
//        }
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

