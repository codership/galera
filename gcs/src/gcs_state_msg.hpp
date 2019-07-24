/*
 * Copyright (C) 2008-2019 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to state messages
 *
 */

#ifndef _gcs_state_msg_h_
#define _gcs_state_msg_h_

#include "gcs.hpp"
#include "gcs_seqno.hpp"
#include "gcs_act_proto.hpp"

#include <unistd.h>
#include <stdbool.h>

/* State flags */
#define GCS_STATE_FREP       0x01 // group representative
#define GCS_STATE_FCLA       0x02 // count last applied (for JOINED node)
#define GCS_STATE_FBOOTSTRAP 0x04 // part of prim bootstrap process
#define GCS_STATE_ARBITRATOR 0x08 // arbitrator or otherwise incomplete node

#ifdef GCS_STATE_MSG_ACCESS
typedef struct gcs_state_msg
{
    gu_uuid_t        state_uuid;    // UUID of the current state exchange
    gu_uuid_t        group_uuid;    // UUID of the group
    gu_uuid_t        prim_uuid;     // last PC state UUID
    gcs_seqno_t      prim_seqno;    // last PC state seqno
    gcs_seqno_t      received;      // last action seqno (received up to)
    gcs_seqno_t      cached;        // earliest action cached
    gcs_seqno_t      last_applied;  // last applied action reported by node
    gcs_seqno_t      vote_seqno;    // last seqno node voted on
    int64_t          vote_res;      // the vote reported by node
    const char*      name;          // human assigned node name
    const char*      inc_addr;      // incoming address string
    int              version;       // version of state message
    int              gcs_proto_ver;
    int              repl_proto_ver;
    int              appl_proto_ver;
    int              prim_gcs_ver;
    int              prim_repl_ver;
    int              prim_appl_ver;
    int              prim_joined;   // number of joined nodes in its last PC
    int              desync_count;
    uint8_t          vote_policy;   // voting policy the node is using
    gcs_node_state_t prim_state;    // state of the node in its last PC
    gcs_node_state_t current_state; // current state of the node
    uint8_t          flags;
}
gcs_state_msg_t;
#else
typedef struct gcs_state_msg gcs_state_msg_t;
#endif

/*! Quorum decisions */
typedef struct gcs_state_quorum
{
    gu_uuid_t   group_uuid;   //! group UUID
    gcs_seqno_t act_id;       //! next global seqno
    gcs_seqno_t conf_id;      //! configuration id
    gcs_seqno_t last_applied; //! group-wide commit cut
    bool        primary;      //! primary configuration or not
    int         version;      //! state excahnge version (max understood by all)
    int         gcs_proto_ver;
    int         repl_proto_ver;
    int         appl_proto_ver;
    uint8_t     vote_policy;
}
gcs_state_quorum_t;

#define GCS_VOTE_ZERO_WINS 1

#define GCS_QUORUM_NON_PRIMARY (gcs_state_quorum_t){    \
        GU_UUID_NIL,                                    \
        GCS_SEQNO_ILL,                                  \
        GCS_SEQNO_ILL,                                  \
        GCS_SEQNO_ILL,                                  \
        false,                                          \
        -1, -1, -1, -1, GCS_VOTE_ZERO_WINS              \
    }

extern gcs_state_msg_t*
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
                      int              gcs_proto_ver,
                      int              repl_proto_ver,
                      int              appl_proto_ver,
                      int              prim_gcs_ver,
                      int              prim_repl_ver,
                      int              prim_appl_ver,
                      int              desync_count,
                      uint8_t          flags);

extern void
gcs_state_msg_destroy (gcs_state_msg_t* state);

/* Returns length needed to serialize gcs_state_msg_t for sending */
extern size_t
gcs_state_msg_len (gcs_state_msg_t* state);

/* Serialize gcs_state_msg_t into message */
extern ssize_t
gcs_state_msg_write (void* msg, const gcs_state_msg_t* state);

/* De-serialize gcs_state_msg_t from message */
extern gcs_state_msg_t*
gcs_state_msg_read (const void* msg, ssize_t msg_len);

/* Get state uuid */
extern const gu_uuid_t*
gcs_state_msg_uuid (const gcs_state_msg_t* state);

/* Get group uuid */
extern const gu_uuid_t*
gcs_state_msg_group_uuid (const gcs_state_msg_t* state);

/* Get last PC uuid */
//extern const gu_uuid_t*
//gcs_state_prim_uuid (const gcs_state_msg_t* state);

/* Get last received action seqno */
extern gcs_seqno_t
gcs_state_msg_received (const gcs_state_msg_t* state);

/* Get lowest cached action seqno */
extern gcs_seqno_t
gcs_state_msg_cached (const gcs_state_msg_t* state);

/* Get current node state */
extern gcs_node_state_t
gcs_state_msg_current_state (const gcs_state_msg_t* state);

/* Get last prim node state */
extern gcs_node_state_t
gcs_state_msg_prim_state (const gcs_state_msg_t* state);

/* Get node name */
extern const char*
gcs_state_msg_name (const gcs_state_msg_t* state);

/* Get node incoming address */
extern const char*
gcs_state_msg_inc_addr (const gcs_state_msg_t* state);

/* Get last applied action seqno */
gcs_seqno_t
gcs_state_msg_last_applied (const gcs_state_msg_t* state);

/* Get last vote */
void
gcs_state_msg_last_vote (const gcs_state_msg_t* state,
                         gcs_seqno_t& seqno, int64_t& res);

/* Get vote policy */
uint8_t
gcs_state_msg_vote_policy (const gcs_state_msg_t* state);

/* Get supported protocols */
extern void
gcs_state_msg_get_proto_ver (const gcs_state_msg_t* state,
                             int* gcs_proto_ver,
                             int* repl_proto_ver,
                             int* appl_proto_ver);

/* Get desync count */
extern int
gcs_state_msg_get_desync_count(const gcs_state_msg_t* state);

/* Get state message flags */
extern uint8_t
gcs_state_msg_flags (const gcs_state_msg_t* state);

/*! Get quorum decision from state messages
 *
 * @param[in]  states      array of state message pointers
 * @param[in]  states_num  length of array
 * @param[out] quorum      quorum calculations result
 * @retval 0 if there were no errors during processing. Quorum results are in
 *         quorum parameter */
extern long
gcs_state_msg_get_quorum (const gcs_state_msg_t* states[],
                          size_t                 states_num,
                          gcs_state_quorum_t*    quorum);

/* Print state message contents to buffer */
extern int
gcs_state_msg_snprintf (char* str, size_t size, const gcs_state_msg_t* msg);

#endif /* _gcs_state_msg_h_ */
