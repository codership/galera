// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * This header defines GC backend interface.
 * Since we can't know backend context in advance,
 * we have to use type void*. Kind of unsafe.
 */

#ifndef _gcs_backend_h_
#define _gcs_backend_h_

#include <stdlib.h>
#include "gcs.h"
#include "gcs_msg_type.h"

typedef struct gcs_backend_conn gcs_backend_conn_t;
typedef struct gcs_backend gcs_backend_t;

/*
 * The macros below are declarations of backend functions
 * (kind of function signatures)
 */
#define GCS_BACKEND_OPEN_FN(fn)           \
long fn (gcs_backend_t*    backend,       \
	 const char* const channel,       \
	 const char* const socket)

#define GCS_BACKEND_CLOSE_FN(fn)          \
long fn (gcs_backend_t*    backend)

/*!
 * Send a message from the backend.
 *
 * @param backend
 *        a pointer to the backend handle 
 * @param buf
 *        a buffer to copy the message to 
 * @param len
 *        length of the supplied buffer
 * @param msg_type
 *        type of the message
 * @return
 *        negative error code in case of error
 *        OR
 *        amount of bytes sent
 */
#define GCS_BACKEND_SEND_FN(fn)           \
long fn (gcs_backend_t* const backend,    \
	 const void*    const buf,        \
	 size_t         const len,        \
	 gcs_msg_type_t const msg_type)

/*!
 * Receive a message from the backend.
 *
 * @param backend
 *        a pointer to the backend object 
 * @param buf
 *        a buffer to copy the message to 
 * @param len
 *        length of the supplied buffer
 * @param msg_type
 *        type of the message
 * @param sender_id
 *        unique sender ID in this configuration
 * @return
 *        negative error code in case of error
 *        OR
 *        the length of the message, so if it is bigger
 *        than len, it has to be reread with a bigger buffer
 */
#define GCS_BACKEND_RECV_FN(fn)           \
long fn (gcs_backend_t*  const backend,   \
	 void*           const buf,       \
	 size_t          const len,       \
	 gcs_msg_type_t* const msg_type,  \
	 long*           const sender_id)

/* for lack of better place define it here */
static const long GCS_SENDER_NONE = -1; /** When there's no sender */

/*! Returns symbolic name of the backend */
#define GCS_BACKEND_NAME_FN(fn)           \
const char* fn (void)

/*!
 * Returns the size of the message such that network packet won't exceed
 * given value.
 *
 * @param backend
 *        backend handle
 * @param pkt_size
 *        desired size of a network packet
 * @return
 *        - message size coresponding to the desired packet size OR
 *        - maximum message size the backend supports if requested packet size
 *          is too big OR
 *        - negative amount by which the packet size must be increased in order
 *          to send at least 1 byte.
 */
#define GCS_BACKEND_MSG_SIZE_FN(fn)       \
long fn (gcs_backend_t* const backend,    \
         ulong          const pkt_size)

typedef GCS_BACKEND_OPEN_FN     ((*gcs_backend_open_t));
typedef GCS_BACKEND_CLOSE_FN    ((*gcs_backend_close_t));
typedef GCS_BACKEND_SEND_FN     ((*gcs_backend_send_t));
typedef GCS_BACKEND_RECV_FN     ((*gcs_backend_recv_t));
typedef GCS_BACKEND_NAME_FN     ((*gcs_backend_name_t));
typedef GCS_BACKEND_MSG_SIZE_FN ((*gcs_backend_msg_size_t));

struct gcs_backend
{
    gcs_backend_conn_t*    conn;
    gcs_backend_close_t    close;
    gcs_backend_send_t     send;
    gcs_backend_recv_t     recv;
    gcs_backend_name_t     name;
    gcs_backend_msg_size_t msg_size;
};

/*!
 * Initializes preallocated backend object and opens backend connection
 * (sort of like 'new')
 */
long
gcs_backend_init (gcs_backend_t*     const bk,
		  const char*        const channel,
		  const char*        const sock_addr,
		  gcs_backend_type_t const backend);

#endif /* _gcs_backend_h_ */
