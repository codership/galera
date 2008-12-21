/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/* 
 * Dummy backend implementation
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

#include <galerautils.h>

#include "gcs_comp_msg.h"
#include "gcs_dummy.h"

typedef struct dummy_msg
{
    gcs_msg_type_t type;
    size_t         len;
    uint8_t        buf[0];
}
dummy_msg_t;

typedef struct gcs_backend_conn
{
    gu_fifo_t   *gc_q;   /* "serializator" */
    dummy_msg_t *msg;    /* last undelivered message */
    bool         closed;
    gcs_seqno_t  msg_id;
    size_t       msg_max_size;
}
dummy_t;

static inline dummy_msg_t*
dummy_msg_create (gcs_msg_type_t const type,
		  size_t         const len,
		  const void*    const buf)
{
    dummy_msg_t *msg = NULL;

    if ((msg = gu_malloc (sizeof(dummy_msg_t) + len)))
    {
	    memcpy (msg->buf, buf, len);
	    msg->len  = len;
	    msg->type = type;
    }
    
    return msg;
}

static inline long
dummy_msg_destroy (dummy_msg_t **msg)
{
    if (*msg)
    {
	gu_free (*msg);
	*msg = NULL;
    }
    return 0;
}

static
GCS_BACKEND_DESTROY_FN(dummy_destroy)
{
    dummy_t* dummy = backend->conn;
    
    if (!dummy || !dummy->closed) return -EBADFD;

//    gu_debug ("Deallocating message queue (serializer)");
    gu_fifo_destroy  (dummy->gc_q);
//    gu_debug ("Freeing message object.");
    dummy_msg_destroy (&dummy->msg);
    gu_free (dummy);
    backend->conn = NULL;
    return 0;
}

static
GCS_BACKEND_SEND_FN(dummy_send)
{
    int err = 0;

    if (backend->conn)
    {
	dummy_msg_t *msg   = dummy_msg_create (msg_type, len, buf);
	if (msg)
	{
            dummy_msg_t** ptr = gu_fifo_get_tail (backend->conn->gc_q);
	    if (gu_likely(ptr != NULL)) {
                *ptr = msg;
                gu_fifo_push_tail (backend->conn->gc_q);
                return len;
            }
	    else {
		dummy_msg_destroy (&msg);
		err = -EBADFD; // closed
	    }
	}
	else
	    err = -ENOMEM;
    }
    else
	err = -EBADFD;

    return err;
}

static
GCS_BACKEND_RECV_FN(dummy_recv)
{
    int ret = 0;
    dummy_t* conn = backend->conn;

    *sender_id = GCS_SENDER_NONE;
    *msg_type  = GCS_MSG_ERROR;

    assert (conn);

    /* skip it if we already have popped a message from the queue
     * in the previous call */
    if (!conn->msg)
    {
        dummy_msg_t** ptr = gu_fifo_get_head (conn->gc_q);
        if (gu_likely(ptr != NULL)) {
            /* Always the same sender */
            conn->msg = *ptr;
            gu_fifo_pop_head (conn->gc_q);
        }
        else {
            ret = -EBADFD; // closing
            gu_debug ("Returning %d: %s", ret, strerror(-ret));
            return ret;
        }
    }

    *sender_id=0;	    
    assert (conn->msg);
    ret = conn->msg->len;
    
    if (conn->msg->len <= len)
    {
        memcpy (buf, conn->msg->buf, conn->msg->len);
        *msg_type = conn->msg->type;
        dummy_msg_destroy (&conn->msg);
    }
    else {
        memcpy (buf, conn->msg->buf, len);
    }

    return ret;
}

static
GCS_BACKEND_NAME_FN(dummy_name)
{
    return "built-in dummy backend";
}

static
GCS_BACKEND_MSG_SIZE_FN(dummy_msg_size)
{
    long max_size = backend->conn->msg_max_size;
    if (pkt_size <= max_size) {
        return (pkt_size - sizeof(dummy_msg_t));
    }
    else {
	gu_warn ("Requested packet size: %d, maximum possible packet size: %d",
		 pkt_size, max_size);
        return (max_size - sizeof(dummy_msg_t));
    }

}

static
GCS_BACKEND_OPEN_FN(dummy_open)
{
    long     ret   = -ENOMEM;
    dummy_t* dummy = backend->conn;
    gcs_comp_msg_t* comp;

    if (!dummy) {
        gu_debug ("Backend not initialized");
        return -EBADFD;
    }

    comp = gcs_comp_msg_new (true, 0, 1);

    if (comp) {
	ret = gcs_comp_msg_add (comp, "Dummy localhost");
	assert (0 == ret); // we have only one member, index = 0
        // put it in the queue, like a usual message 
        ret = gcs_comp_msg_size(comp);
        ret = dummy_send (backend, comp, ret, GCS_MSG_COMPONENT);
        if (ret > 0) ret = 0;
	gcs_comp_msg_delete (comp);
    }
    gu_debug ("Opened backend connection: %d (%s)", ret, strerror(-ret));
    return ret;
}

static
GCS_BACKEND_CLOSE_FN(dummy_close)
{
    long     ret   = -ENOMEM;
    dummy_t* dummy = backend->conn;
    gcs_comp_msg_t* comp;
    
    if (!dummy) return -EBADFD;

    comp = gcs_comp_msg_leave ();

    if (comp) {
        ret = gcs_comp_msg_size(comp);
        ret = dummy_send (backend, comp, ret, GCS_MSG_COMPONENT);
        // FIXME: here's a race condition - some other thread can send something
        // after leave message.
        gu_fifo_close (dummy->gc_q);
        if (ret > 0) ret = 0;
	gcs_comp_msg_delete (comp);
    }
    return ret;
}

static
const gcs_backend_t dummy_backend =
{
    .conn     = NULL,
    .open     = dummy_open,
    .close    = dummy_close,
    .destroy  = dummy_destroy,
    .send     = dummy_send,
    .recv     = dummy_recv,
    .name     = dummy_name,
    .msg_size = dummy_msg_size
};

GCS_BACKEND_CREATE_FN(gcs_dummy_create)
{
    long     ret   = -ENOMEM;
    dummy_t *dummy = NULL;

    if (!(dummy = GU_MALLOC(dummy_t)))
	goto out0;
    if (!(dummy->gc_q = gu_fifo_create (100000,sizeof(void*))))
	goto out1;

    dummy->msg          = NULL;
    dummy->msg_id       = 0;
    dummy->closed       = true;
    dummy->msg_max_size = sysconf (_SC_PAGESIZE);

    *backend      = dummy_backend; // set methods
    backend->conn = dummy;         // set data

    return 0;

out1:
    gu_free (dummy);
out0:
    backend->conn = NULL;
    return ret;
}

