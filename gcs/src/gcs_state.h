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

#ifdef GCS_STATE_ACCESS
typedef struct gcs_state
{
    gcs_seqno_t act_id;    // next action id (received up to)
    gcs_seqno_t comp_id;   // component message id (id of this state exchange)
    gcs_seqno_t conf_id;   // last primary configuration id
    bool        joined;    // if the node contains complete state info
    long        prim_idx;  // if the node comes from primary configuration
                           // its index in the configuraiton, otherwise -1
    const char* name;      // human assigned node name
    const char* inc_addr;  // incoming address string
    gcs_proto_t proto_min;
    gcs_proto_t proto_max;
}
gcs_state_t;
#else
typedef struct gcs_state gcs_state_t;
#endif

extern gcs_state_t*
gcs_state_create (gcs_seqno_t act_id,
                  gcs_seqno_t comp_id,
                  gcs_seqno_t conf_id,
                  bool        joined,
                  long        prim_idx,
                  const char* name,
                  const char* inc_addr,
                  gcs_proto_t proto_min,
                  gcs_proto_t proto_max);

extern void
gcs_state_destroy (gcs_state_t* state);

/* Returns length needed to serialize gcs_state_msg_t for sending */
extern ssize_t
gcs_state_msg_len (gcs_state_t* state);

/* Serialize gcs_state_t into message */
extern ssize_t
gcs_state_msg_write (void* msg, const gcs_state_t* state);

/* De-serialize gcs_state_t from message */
extern gcs_state_t*
gcs_state_msg_read (const void* msg, size_t msg_len);

/* Print state message contents to buffer */
extern int
gcs_state_snprintf (char* str, size_t size, const gcs_state_t* msg);

#endif /* _gcs_state_h_ */
