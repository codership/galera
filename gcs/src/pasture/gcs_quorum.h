// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Quorum object
 *
 */

#ifndef _gcs_quorum_h_
#define _gcs_quorum_h_

#include "gcs_uuid.h"
#include "gcs_comp_msg.h"

/** Result of the quorum calculation */
typedef enum {
    GCS_QUORUM_NO,
    GCS_QUORUM_YES,
    GCS_QUORUM_UNKNOWN
} gcs_quorum_status_t;

/** State of the quorum object */
typedef enum {
    GCS_QUORUM_INIT,
    GCS_QUORUM_VOTING,
    GCS_QUORUM_DONE,
    GCS_QUORUM_ERROR
} gcs_quorum_state_t;

/** Initialization flags */
typedef enum {
    GCS_QUORUM_AUTO, /** Automatically assume quorum, if I'm the only one
                      *  in the first conf. */
    GCS_QUORUM_WAIT  /** Wait to join the quorum component. */
} gcs_quorum_init_t;

/** A quorum voting message */
typedef struct {
    gcs_uuid_t uuid;
    int32_t    prim_id;
    int32_t    status;
} __attribute__((packed))
gcs_vote_msg_t;

typedef struct gcs_quorum gcs_quorum_t;

/** Creates quorum object. */
extern gcs_quorum_t*
gcs_quorum_create (gcs_quorum_init_t init_cond);

/** Destroys quorum object. */
extern void
gcs_quorum_destroy (gcs_quorum_t* quorum);

/** Processes new membership message and returns the vote message. */
extern const gcs_vote_msg_t*
gcs_quorum_memb (gcs_quorum_t* quorum, const gcs_comp_msg_t* comp);

/** Processes quorum vote and returns state of voting. */
extern gcs_quorum_state_t
gcs_quorum_vote (gcs_quorum_t* quorum, const gcs_vote_msg_t* vote,
		 size_t vote_len, long sender_id);

/** Returns quorum status */
extern gcs_quorum_status_t
gcs_quorum_status (const gcs_quorum_t* quorum);

/** Assume quorum unconditionnaly. */
extern void
gcs_quorum_force (gcs_quorum_t* quorum);


#endif /* _gcs_quorum_h_ */
