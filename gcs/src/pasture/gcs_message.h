// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Declarations for the message IO layer unit. 
 */

#ifndef _gcs_message_h_
#define _gcs_message_h_

#include "gcs.h"
#include "gcs_msg_type.h"
#include "gcs_backend.h"

typedef struct gcs_message_conn gcs_message_conn_t;

extern int
gcs_message_open (gcs_message_conn_t**     msg_conn,
                  const char* const        channel,
                  const char* const        socket,
                  gcs_backend_type_t const backend_type);

/* REMOVE sent size should be returned by send and recv functions
extern size_t
gcs_message_size (const gcs_message_conn_t* const conn);
*/

extern ssize_t
gcs_message_send (gcs_message_conn_t* msg_conn,
                  const void*         buf,
                  size_t              buf_len);

extern ssize_t
gcs_message_recv (gcs_message_conn_t* msg_conn,
                  void*               buf,
                  size_t              buf_len,
                  gcs_msg_type_t*     msg_type,
                  long*                sender_id);

extern int
gcs_message_close (gcs_message_conn_t** msg_conn);

#endif // _gcs_message_h_
