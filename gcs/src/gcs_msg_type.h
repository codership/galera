/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Message types. 
 */

#ifndef _gcs_msg_type_h_
#define _gcs_msg_type_h_

typedef enum gcs_msg_type
{
    GCS_MSG_ERROR,     // error happened when recv()
    GCS_MSG_ACTION,    // action fragment
    GCS_MSG_LAST,      // report about last applied action
    GCS_MSG_FLOW,      // flow control message
    GCS_MSG_COMPONENT, // new component
    GCS_MSG_STATE_UUID,// state exchange UUID message
    GCS_MSG_STATE_MSG, // state exchange message
    GCS_MSG_JOIN,      // massage saying that the node completed state transfer
    GCS_MSG_SYNC,      // message saying that the node has synced with group
    GCS_MSG_OTHER      // some other message we don't care about yet
}
gcs_msg_type_t;

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
