/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Interface to state messages
 *
 */

#ifndef _gcs_state_h_
#define _gcs_state_h_

#include <unistd.h>
#include <stdbool.h>
#include "gcs_seqno.h"
#include "gcs_act_proto.h"

/* State flags */
#define GCS_STATE_FREP 0x01  // group representative

/* Possible node states */
/* @note: when changing this don't forget to change gcs_state_node_string[] in
 * gcs_state.c */
typedef enum gcs_state_node
{
    GCS_STATE_NON_PRIM,      // comes from non-primary configuration
    GCS_STATE_PRIM,          // comes from primary configuration, empty
    GCS_STATE_JOINER,        // comes from primary conf, requests state trnsfer
    GCS_STATE_DONOR,         // comes from primary conf, donates state transfer
    GCS_STATE_JOINED,        // comes from primary conf, joined
    GCS_STATE_SYNCED,        // comes from primary conf, joined, synced
    GCS_STATE_MAX
}
gcs_state_node_t;

extern const char* gcs_state_node_string[GCS_STATE_MAX];

#ifdef GCS_STATE_ACCESS
typedef struct gcs_state
{
    gu_uuid_t        state_uuid;    // UUID of the current state exchange
    gu_uuid_t        group_uuid;    // UUID of the group
    gu_uuid_t        prim_uuid;     // UUID of the last PC
    long             prim_joined;   // number of joined nodes in the last PC
    gcs_seqno_t      prim_seqno;    // last primary configuration seqno
    gcs_seqno_t      act_seqno;     // last action seqno (received up to)
    gcs_state_node_t prim_state;    // state of the node in the last PC
    gcs_state_node_t current_state; // current state of the node
    const char*      name;          // human assigned node name
    const char*      inc_addr;      // incoming address string
    gcs_proto_t      proto_min;
    gcs_proto_t      proto_max;
    uint8_t          flags;
}
gcs_state_t;
#else
typedef struct gcs_state gcs_state_t;
#endif

/*! Quorum decisions */
typedef struct gcs_state_quorum
{
    gu_uuid_t   group_uuid; // group UUID
    gcs_seqno_t act_id;     // next global seqno
    gcs_seqno_t conf_id;    // configuration id
    bool        primary;    // primary configuration or not
    long        proto;      // protocol to use
}
gcs_state_quorum_t;

extern gcs_state_t*
gcs_state_create (const gu_uuid_t* state_uuid,
                  const gu_uuid_t* group_uuid,
                  const gu_uuid_t* prim_uuid,
                  long             prim_joined,
                  gcs_seqno_t      prim_seqno,
                  gcs_seqno_t      act_seqno,
                  gcs_state_node_t prim_state,
                  gcs_state_node_t current_state,
                  const char*      name,
                  const char*      inc_addr,
                  gcs_proto_t      proto_min,
                  gcs_proto_t      proto_max,
                  uint8_t          flags);

extern void
gcs_state_destroy (gcs_state_t* state);

/* Returns length needed to serialize gcs_state_msg_t for sending */
extern size_t
gcs_state_msg_len (gcs_state_t* state);

/* Serialize gcs_state_t into message */
extern ssize_t
gcs_state_msg_write (void* msg, const gcs_state_t* state);

/* De-serialize gcs_state_t from message */
extern gcs_state_t*
gcs_state_msg_read (const void* msg, size_t msg_len);

/* Get state uuid */
extern const gu_uuid_t*
gcs_state_uuid (const gcs_state_t* state);

/* Get group uuid */
extern const gu_uuid_t*
gcs_state_group_uuid (const gcs_state_t* state);

/* Get action seqno */
extern gcs_seqno_t
gcs_state_act_id (const gcs_state_t* state);

/* Get node state */
extern gcs_state_node_t
gcs_state_node_state (const gcs_state_t* state);

/* Get node name */
extern const char*
gcs_state_name (const gcs_state_t* state);

/* Get node incoming address */
extern const char*
gcs_state_inc_addr (const gcs_state_t* state);

/* Get supported protocols */
extern gcs_proto_t
gcs_state_proto_min (const gcs_state_t* state);
extern gcs_proto_t
gcs_state_proto_max (const gcs_state_t* state);

/* Get state message flags */
extern uint8_t
gcs_state_flags (const gcs_state_t* state);

/* Get quorum decision from state messages */
extern long
gcs_state_get_quorum (const gcs_state_t*  states[],
                      long                states_num,
                      gcs_state_quorum_t* quorum);

/* Print state message contents to buffer */
extern int
gcs_state_snprintf (char* str, size_t size, const gcs_state_t* msg);

#endif /* _gcs_state_h_ */
