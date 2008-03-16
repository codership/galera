// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Implementation of the message IO layer.
 * See gcs_message.h 
 */

#include <string.h> // for mempcpy
#include <errno.h>
#include <pthread.h>

#include "gcs_generic.h"
#include "gcs_backend.h"
#include "gcs_message.h"
#include "gcs_comp_msg.h"
#include "gcs_fifo.h"

/* Maximum undelivered messages (must be power of 2) */
#define GCS_MESSAGE_MAX_UNDELIVERED 4

typedef enum conf
{
    PRIMARY,
    NON_PRIMARY
}
conf_t;

struct gcs_message_conn /* gcs_message_conn_t */
{
    /* connection per se */
    volatile size_t pending;
    size_t          max_pending;
    volatile int    config_id;
    int             my_id;
    volatile conf_t config;

    /* backend part */
    gcs_backend_t  *backend; // the context that is returned by
                             // the backend group communication system
};

int gcs_message_open (gcs_message_conn_t**     msg_conn,
		      const char*        const channel,
		      const char*        const socket,
		      gcs_backend_type_t const backend_type)
{
    int                 err = 0;
    gcs_message_conn_t *conn = GU_CALLOC (1, gcs_message_conn_t);

    if (NULL == conn) return -ENOMEM;

    err = gcs_backend_init (backend_type);
    if (err < 0) return err;

    gu_info ("Opening connection to channel: %s, socket: %s, backend: %s",
             channel, socket, gcs_backend_string());

    err = gcs_backend_open (&conn->backend, channel, socket); 
    if (err < 0) return err;
    assert (0 == err);
    assert (conn->backend);

    /* good initial guess */
    conn->max_pending = GCS_MESSAGE_MAX_UNDELIVERED;
    conn->config      = NON_PRIMARY;

    *msg_conn = conn;
    return 0;
}

#if 0 // REMOVE
size_t gcs_message_size (const gcs_message_conn_t* const conn)
{
    return conn->backend_msg_size;
}
#endif

typedef struct
{
    size_t  msg_size;
    uint8_t msg[0];
}
send_buf_t;

ssize_t
gcs_message_send (gcs_message_conn_t* conn,
                  const void*         buf,
                  size_t              buf_len)
{
    ssize_t     ret;

    if (conn->pending == conn->max_pending) return -EAGAIN;

    /* update tail position and pending count - must be made in advance
     * to avoid a race with recv() thread in the abscence of locking */
    conn->pending++;
    if (0 > (ret = gcs_backend_send (conn->backend,
                                     buf,
                                     buf_len,
				     GCS_MSG_ACTION))) {
        conn->pending--;
        goto out;
    }

    assert (ret == buf_len);

out:
    return ret;
}

/*
 * Handles primary configuration action - installs new membership,
 * cleans old one, renews process id and signals waiting send threads
 * to continue.
 */
static int
handle_primary (gcs_message_conn_t* conn, const gcs_comp_msg_t* comp)
{
    if (PRIMARY == conn->config) {
        /* we come from previous primary configuration */
	conn->my_id = gcs_comp_msg_self (comp);
        conn->config_id++;
        return 0;
    }
    else {
        gu_debug ("Discontinuity in primary configurations!");
        gu_debug ("State snapshot is needed!");
        return -ENOTRECOVERABLE; // FIXME!!!
    }
}

static
int handle_non_primary (gcs_message_conn_t* conn)
{
    conn->config = NON_PRIMARY;
    return 0;
}

/*! Receives message */
ssize_t
gcs_message_recv (gcs_message_conn_t* conn,
                  void*               buf,
                  size_t              buf_len,
                  gcs_msg_type_t*     msg_type,
                  long*               sender_id)

{
    int ret = gcs_backend_recv (conn->backend, buf, buf_len,
                                msg_type, sender_id);

    if (ret < 0 || ret > buf_len) return ret; // error or buffer too small

    switch (*msg_type) {
    case GCS_MSG_ACTION:

#ifdef GCS_DEBUG_GENERIC
        // perhaps this should be an assert?
        if (conn->config != PRIMARY) {
            gu_debug ("Action message in non-primary configuration!\n");
            //break;
        }
#endif
        assert (conn->pending > 0);
        conn->pending -= (*sender_id == conn->my_id);
        break;
    case GCS_MSG_COMPONENT:
	if (gcs_comp_msg_primary(buf)) {
	    handle_primary (conn, buf);
	    gu_debug ("Received PRIM message\n");
	} else {
	    handle_non_primary (conn);
	    gu_debug ("Received NON_PRIM message\n");
	}
        break;
    default:
        // this normaly should not happen, shall we bother with
        // protection?
        break;
    }

    return ret;
}

int
gcs_message_close (gcs_message_conn_t** msg_conn)
{
    gcs_message_conn_t *conn = *msg_conn;

    if (!conn) return -ENOTCONN;

    gcs_backend_close (&conn->backend);

    gu_free (conn);
    *msg_conn = NULL;
    return 0;
}

