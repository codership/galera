/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
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

#define GCS_COMP_MSG_ACCESS // for gcs_comp_memb_t

#ifndef GCS_DUMMY_TESTING
#define GCS_DUMMY_TESTING
#endif

#include "gcs_dummy.hpp"

typedef struct dummy_msg
{
    gcs_msg_type_t type;
    ssize_t        len;
    long           sender_idx;
    uint8_t        buf[];
}
dummy_msg_t;

static inline dummy_msg_t*
dummy_msg_create (gcs_msg_type_t const type,
                  size_t         const len,
                  long           const sender,
                  const void*    const buf)
{
    dummy_msg_t *msg = NULL;

    if ((msg = static_cast<dummy_msg_t*>(gu_malloc (sizeof(dummy_msg_t) + len))))
    {
        memcpy (msg->buf, buf, len);
        msg->len        = len;
        msg->type       = type;
        msg->sender_idx = sender;
    }

    return msg;
}

static inline long
dummy_msg_destroy (dummy_msg_t *msg)
{
    if (msg)
    {
        gu_free (msg);
    }
    return 0;
}

typedef enum dummy_state
{
    DUMMY_DESTROYED,
    DUMMY_CLOSED,
    DUMMY_NON_PRIM,
    DUMMY_TRANS,
    DUMMY_PRIM,
}
dummy_state_t;

typedef struct gcs_backend_conn
{
    gu_fifo_t*       gc_q;   /* "serializator" */
    volatile dummy_state_t    state;
    gcs_seqno_t      msg_id;
    const size_t     max_pkt_size;
    const size_t     hdr_size;
    const size_t     max_send_size;
    long             my_idx;
    long             memb_num;
    gcs_comp_memb_t* memb;
}
dummy_t;

static
GCS_BACKEND_DESTROY_FN(dummy_destroy)
{
    dummy_t*      dummy = backend->conn;

    if (!dummy || dummy->state != DUMMY_CLOSED) return -EBADFD;

//    gu_debug ("Deallocating message queue (serializer)");
    gu_fifo_destroy  (dummy->gc_q);
    if (dummy->memb) gu_free (dummy->memb);
    gu_free (dummy);
    backend->conn = NULL;
    return 0;
}

static
GCS_BACKEND_SEND_FN(dummy_send)
{
    int err = 0;
    dummy_t* dummy = backend->conn;

    if (gu_unlikely(NULL == dummy)) return -EBADFD;

    if (gu_likely(DUMMY_PRIM == dummy->state))
    {
        err = gcs_dummy_inject_msg (backend, buf, len, msg_type,
                                    backend->conn->my_idx);
    }
    else {
        static long send_error[DUMMY_PRIM] =
            { -EBADFD, -EBADFD, -ENOTCONN, -EAGAIN };
        err = send_error[dummy->state];
    }

    return err;
}

static
GCS_BACKEND_RECV_FN(dummy_recv)
{
    long     ret = 0;
    dummy_t* conn = backend->conn;

    msg->sender_idx = GCS_SENDER_NONE;
    msg->type   = GCS_MSG_ERROR;

    assert (conn);

    /* skip it if we already have popped a message from the queue
     * in the previous call */
    if (gu_likely(DUMMY_CLOSED <= conn->state))
    {
        int err;
        dummy_msg_t** ptr = static_cast<dummy_msg_t**>(
            gu_fifo_get_head (conn->gc_q, &err));

        if (gu_likely(ptr != NULL)) {

            dummy_msg_t* dmsg = *ptr;

            assert (NULL != dmsg);

            msg->type       = dmsg->type;
            msg->sender_idx = dmsg->sender_idx;
            ret             = dmsg->len;
            msg->size       = ret;

            if (gu_likely(dmsg->len <= msg->buf_len)) {
                gu_fifo_pop_head (conn->gc_q);
                memcpy (msg->buf, dmsg->buf, dmsg->len);
                dummy_msg_destroy (dmsg);
            }
            else {
                // supplied recv buffer too short, leave the message in queue
                memcpy (msg->buf, dmsg->buf, msg->buf_len);
                gu_fifo_release (conn->gc_q);
            }
        }
        else {
            ret = -EBADFD; // closing
            gu_debug ("Returning %d: %s", ret, strerror(-ret));
        }
    }
    else {
        ret = -EBADFD;
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
    const long max_pkt_size = backend->conn->max_pkt_size;

    if (pkt_size > max_pkt_size) {
        gu_warn ("Requested packet size: %d, maximum possible packet size: %d",
                 pkt_size, max_pkt_size);
        return (max_pkt_size - backend->conn->hdr_size);
    }

    return (pkt_size - backend->conn->hdr_size);
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

    if (!bootstrap) {
        dummy->state = DUMMY_TRANS;
        return 0;
    }

    comp = gcs_comp_msg_new (true, false, 0, 1, 0);

    if (comp) {
        ret = gcs_comp_msg_add (comp, "11111111-2222-3333-4444-555555555555",0);
        assert (0 == ret); // we have only one member, index = 0

        dummy->state = DUMMY_TRANS; // required by gcs_dummy_set_component()
        ret = gcs_dummy_set_component (backend, comp); // install new component
        if (ret >= 0) {                                // queue the message
            ret = gcs_comp_msg_size(comp);
            ret = gcs_dummy_inject_msg (backend, comp, ret, GCS_MSG_COMPONENT,
                                        GCS_SENDER_NONE);
            if (ret > 0) ret = 0;
        }
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

    comp = gcs_comp_msg_leave (0);

    if (comp) {
        ret = gcs_comp_msg_size(comp);
        ret = gcs_dummy_inject_msg (backend, comp, ret, GCS_MSG_COMPONENT,
                                    GCS_SENDER_NONE);
        // Here's a race condition - some other thread can send something
        // after leave message. But caller should guarantee serial access.
        gu_fifo_close (dummy->gc_q);
        if (ret > 0) ret = 0;
        gcs_comp_msg_delete (comp);
    }

    dummy->state = DUMMY_CLOSED;

    return ret;
}

static
GCS_BACKEND_PARAM_SET_FN(dummy_param_set)
{
    return 1;
}

static
GCS_BACKEND_PARAM_GET_FN(dummy_param_get)
{
    return NULL;
}

GCS_BACKEND_STATUS_GET_FN(dummy_status_get)
{
}

GCS_BACKEND_CREATE_FN(gcs_dummy_create)
{
    long     ret   = -ENOMEM;
    dummy_t* dummy = NULL;

    if (!(dummy = GU_CALLOC(1, dummy_t)))
        goto out0;

    dummy->state = DUMMY_CLOSED;
    *(size_t*)(&dummy->max_pkt_size)  = (size_t) sysconf (_SC_PAGESIZE);
    *(size_t*)(&dummy->hdr_size)      = sizeof(dummy_msg_t);
    *(size_t*)(&dummy->max_send_size) = dummy->max_pkt_size - dummy->hdr_size;

    if (!(dummy->gc_q = gu_fifo_create (1 << 16, sizeof(void*))))
        goto out1;

    backend->conn      = NULL;
    backend->open      = dummy_open;
    backend->close     = dummy_close;
    backend->destroy   = dummy_destroy;
    backend->send      = dummy_send;
    backend->recv      = dummy_recv;
    backend->name      = dummy_name;
    backend->msg_size  = dummy_msg_size;
    backend->param_set = dummy_param_set;
    backend->param_get = dummy_param_get;
    backend->status_get = dummy_status_get;

    backend->conn = dummy;         // set data

    return 0;

out1:
    gu_free (dummy);
out0:
    backend->conn = NULL;
    return ret;
}

GCS_BACKEND_REGISTER_FN(gcs_dummy_register) { return false; }

/*! Injects a message in the message queue to produce a desired msg sequence. */
long
gcs_dummy_inject_msg (gcs_backend_t* backend,
                      const void*    buf,
                      size_t         buf_len,
                      gcs_msg_type_t type,
                      long           sender_idx)
{
    long         ret;
    size_t       send_size = buf_len < backend->conn->max_send_size ?
                             buf_len : backend->conn->max_send_size;
    dummy_msg_t* msg = dummy_msg_create (type, send_size, sender_idx, buf);

    if (msg)
    {
        dummy_msg_t** ptr = static_cast<dummy_msg_t**>(
            gu_fifo_get_tail (backend->conn->gc_q));

        if (gu_likely(ptr != NULL)) {
            *ptr = msg;
            gu_fifo_push_tail (backend->conn->gc_q);
            ret = send_size;
        }
        else {
            dummy_msg_destroy (msg);
            ret = -EBADFD; // closed
        }
    }
    else {
        ret = -ENOMEM;
    }

    return ret;
}

/*! Sets the new component view.
 *  The same component message should be injected in the queue separately
 *  (see gcs_dummy_inject_msg()) in order to model different race conditions */
long
gcs_dummy_set_component (gcs_backend_t*        backend,
                         const gcs_comp_msg_t* comp)
{
    dummy_t* dummy    = backend->conn;
    long     new_num  = gcs_comp_msg_num (comp);
    long     i;

    assert (dummy->state > DUMMY_CLOSED);

    if (dummy->memb_num != new_num) {
        void* tmp = gu_realloc (dummy->memb, new_num * sizeof(gcs_comp_memb_t));

        if (NULL == tmp) return -ENOMEM;

        dummy->memb     = static_cast<gcs_comp_memb_t*>(tmp);
        dummy->memb_num = new_num;
    }

    for (i = 0; i < dummy->memb_num; i++) {
        strcpy ((char*)&dummy->memb[i], gcs_comp_msg_member(comp, i)->id);
    }

    dummy->my_idx = gcs_comp_msg_self(comp);
    dummy->state  = gcs_comp_msg_primary(comp) ? DUMMY_PRIM : DUMMY_NON_PRIM;
    gu_debug ("Setting state to %s",
              DUMMY_PRIM == dummy->state ? "DUMMY_PRIM" : "DUMMY_NON_PRIM");

    return 0;
}

/*! Is needed to set transitional state */
long
gcs_dummy_set_transitional (gcs_backend_t* backend)
{
    backend->conn->state = DUMMY_TRANS;
    return 0;
}
