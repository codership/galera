/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * This header defines GC backend interface.
 * Since we can't know backend context in advance,
 * we have to use type void*. Kind of unsafe.
 */

#ifndef _gcs_backend_h_
#define _gcs_backend_h_

#include "gcs.hpp"
#include "gcs_recv_msg.hpp"

#include <galerautils.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct gcs_backend_conn gcs_backend_conn_t;
typedef struct gcs_backend      gcs_backend_t;

/*
 * The macros below are declarations of backend functions
 * (kind of function signatures)
 */

/*! Registers configuration parameters with config */
#define GCS_BACKEND_REGISTER_FN(fn)       \
bool fn (gu_config_t* cnf)

/*! Allocates backend context and sets up the backend structure */
#define GCS_BACKEND_CREATE_FN(fn)         \
long fn (gcs_backend_t*     backend,      \
         const char*  const addr,         \
         gu_config_t* const cnf)

/*! Deallocates backend context */
#define GCS_BACKEND_DESTROY_FN(fn)        \
long fn (gcs_backend_t*    backend)

/*! Puts backend handle into operating state */
#define GCS_BACKEND_OPEN_FN(fn)           \
long fn (gcs_backend_t*    backend,       \
         const char* const channel,       \
         bool        const bootstrap)

/*! Puts backend handle into non-operating state */
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
 * @param timeout
 *        absolute timeout date in nanoseconds
 * @return
 *        negative error code in case of error
 *        OR
 *        the length of the message, so if it is bigger
 *        than len, it has to be reread with a bigger buffer
 */
#define GCS_BACKEND_RECV_FN(fn)                 \
long fn (gcs_backend_t*  const backend,         \
         gcs_recv_msg_t* const msg,             \
         long long       const timeout)

/* for lack of better place define it here */
static const long GCS_SENDER_NONE = -1; /** When there's no sender */

/*! Returns symbolic name of the backend */
#define GCS_BACKEND_NAME_FN(fn)           \
const char* fn (void)

/*!
 * Returns the size of the message such that resulting network packet won't
 * exceed given value (basically, pkt_size - headers).
 *
 * @param backend
 *        backend handle
 * @param pkt_size
 *        desired size of a network packet
 * @return
 *        - message size coresponding to the desired network packet size OR
 *        - maximum message size the backend supports if requested packet size
 *          is too big OR
 *        - negative amount by which the packet size must be increased in order
 *          to send at least 1 byte.
 */
#define GCS_BACKEND_MSG_SIZE_FN(fn)         \
long fn (gcs_backend_t* const backend,      \
         long           const pkt_size)

/*!
 * @param backend
 *        backend handle
 * @param key
 *        parameter name
 * @param value
 *        parameter value
 * @return 1 if parameter not recognized, 0 in case of success and negative
 *         error code in case of error
 */
#define GCS_BACKEND_PARAM_SET_FN(fn)  \
long fn (gcs_backend_t* backend,      \
         const char*    key,          \
         const char*    value)

/*!
 * @param backend
 *        backend handle
 * @param key
 *        parameter name
 * @return NULL if parameter not recognized
 */
#define GCS_BACKEND_PARAM_GET_FN(fn)        \
const char* fn (gcs_backend_t* backend,     \
                const char*    key)

/*!
 * @param backend
 *        backend handle
 * @param status
 *        reference to status variable map
 */
#define GCS_BACKEND_STATUS_GET_FN(fn)           \
    void fn(gcs_backend_t* backend,             \
            gu::Status& status)


typedef GCS_BACKEND_CREATE_FN    ((*gcs_backend_create_t));
typedef GCS_BACKEND_DESTROY_FN   ((*gcs_backend_destroy_t));
typedef GCS_BACKEND_OPEN_FN      ((*gcs_backend_open_t));
typedef GCS_BACKEND_CLOSE_FN     ((*gcs_backend_close_t));
typedef GCS_BACKEND_SEND_FN      ((*gcs_backend_send_t));
typedef GCS_BACKEND_RECV_FN      ((*gcs_backend_recv_t));
typedef GCS_BACKEND_NAME_FN      ((*gcs_backend_name_t));
typedef GCS_BACKEND_MSG_SIZE_FN  ((*gcs_backend_msg_size_t));
typedef GCS_BACKEND_PARAM_SET_FN ((*gcs_backend_param_set_t));
typedef GCS_BACKEND_PARAM_GET_FN ((*gcs_backend_param_get_t));
typedef GCS_BACKEND_STATUS_GET_FN ((*gcs_backend_status_get_t));

struct gcs_backend
{
    gcs_backend_conn_t*     conn;
    gcs_backend_open_t      open;
    gcs_backend_close_t     close;
    gcs_backend_destroy_t   destroy;
    gcs_backend_send_t      send;
    gcs_backend_recv_t      recv;
    gcs_backend_name_t      name;
    gcs_backend_msg_size_t  msg_size;
    gcs_backend_param_set_t param_set;
    gcs_backend_param_get_t param_get;
    gcs_backend_status_get_t status_get;
};

/*!
 * Registers backends' parameters with config.
 */
bool
gcs_backend_register(gu_config_t* conf);

/*!
 * Initializes preallocated backend object and opens backend connection
 * (sort of like 'new')
 */
long
gcs_backend_init (gcs_backend_t* bk,
                  const char*    uri,
                  gu_config_t*   cnf);

#endif /* _gcs_backend_h_ */
