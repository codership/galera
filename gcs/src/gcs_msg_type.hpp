/*
 * Copyright (C) 2008-2015 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Message types.
 */

#ifndef _gcs_msg_type_h_
#define _gcs_msg_type_h_

// NOTE! When changing this enumaration, make sure to change
// gcs_msg_type_string[] in gcs_msg_type.c
typedef enum gcs_msg_type
{
    GCS_MSG_ERROR,     // error happened when recv()
    GCS_MSG_ACTION,    // action fragment
    GCS_MSG_LAST,      // report about last applied action
    GCS_MSG_COMPONENT, // new component
    GCS_MSG_STATE_UUID,// state exchange UUID message
    GCS_MSG_STATE_MSG, // state exchange message
    GCS_MSG_JOIN,      // massage saying that the node completed state transfer
    GCS_MSG_SYNC,      // message saying that the node has synced with group
    GCS_MSG_FLOW,      // flow control message
    GCS_MSG_VOTE,      // vote message
    GCS_MSG_CAUSAL,    // causality token
    GCS_MSG_MAX
}
gcs_msg_type_t;

extern const char* gcs_msg_type_string[GCS_MSG_MAX];

/* Types of private actions - should not care,
 * must be defined and used by the application */

/* Types of regular configuration mesages (both PRIM/NON_PRIM) */
typedef enum gcs_reg_type
{
    GCS_REG_JOIN,          // caused by member JOIN
    GCS_REG_LEAVE,         // caused by member LEAVE
    GCS_REG_DISCONNECT,    // caused by member DISCONNECT
    GCS_REG_NETWORK        // caused by NETWORK failure?
}
gcs_reg_type_t;

#endif // _gcs_message_h_
