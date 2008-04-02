// Copyright (C) 2007 Codership Oy <info@codership.com>
/*
 * Implementation of the generic communication layer.
 * See gcs_generic.h 
 */

#include <string.h> // for mempcpy
#include <errno.h>

#include <galerautils.h>

#include "gcs_backend.h"
#include "gcs_comp_msg.h"
#include "gcs_fifo.h"
#include "gcs_act_proto.h"
#include "gcs_generic.h"

#define GENERIC_FIFO_LEN 1<<15 // 32K elements (128/256K bytes)

typedef struct generic_recv_msg
{
    void* buf;
    long  buf_len;
    long  size;
    long  sender_id;    
    gcs_msg_type_t type;
}
generic_recv_msg_t;

typedef struct generic_recv_act
{
    gcs_seqno_t    send_no;
    uint8_t*       head; // head of action buffer
    uint8_t*       tail; // tail of action data
    size_t         size;
    size_t         received;
    gcs_act_type_t type;
}
generic_recv_act_t;

typedef enum generic_state
{
    GENERIC_PRIMARY,
    GENERIC_NON_PRIMARY,
    GENERIC_CLOSED
}
generic_state_t;

struct gcs_generic_conn
{
    /* connection per se */
    long             my_id;
    long             prim_comp_no;
    generic_state_t  state;
    gcs_comp_msg_t*  comp;
//    gu_mutex_t      prim_lock;
//    gu_cond_t       prim_cond;
//    gu_mutex_t      lock; // must get rid of it.

    /* protocol */
    long            proto_ver;

    /* send part */
    gu_mutex_t      send_lock;
    void*           send_buf;
    size_t          send_buf_len;
    gcs_seqno_t     send_act_no;

    /* local action FIFO */
    gcs_fifo_t*     fifo;

    /* recv part */
    gu_mutex_t          recv_lock;
    generic_recv_msg_t  recv_msg;
    /* array of actions currently being received */
    generic_recv_act_t *recv_acts;
    long                recv_acts_num;
    gcs_seqno_t         recv_act_no;  // actions must be numbered in the upper
                                      // layer, remove in future refactoring

    /* backend part */
    size_t           msg_size;
    gcs_backend_t    backend;         // message IO context
};

long gcs_generic_open (gcs_generic_conn_t **generic,
		       const char* const channel,
		       const char* const backend_uri)
{
    long                err = 0;
    gcs_generic_conn_t *conn = GU_CALLOC (1, gcs_generic_conn_t);

    if (NULL == conn) return -ENOMEM;

    gu_debug ("Opening connection to backend IO layer");
    err = gcs_backend_init (&conn->backend, channel, backend_uri);
    if (err < 0) return err;
    assert (0 == err);
    assert (NULL != conn->backend.conn);

    // Need to allocate something, otherwise Spread 3.17.3 freaks out.
#define RECV_INIT_BUF 4096
    conn->recv_msg.buf = gu_malloc(RECV_INIT_BUF);
    if (!conn->recv_msg.buf) {
        err = -ENOMEM;
        goto out;
    }
    conn->recv_msg.buf_len = RECV_INIT_BUF;
#undef RECV_INIT_BUF

    conn->fifo = gcs_fifo_create (GENERIC_FIFO_LEN);
    if (!conn->fifo) {
	err = -ENOMEM;
	goto out1;
    }

    conn->state = GENERIC_NON_PRIMARY;

    gu_mutex_init (&conn->send_lock, NULL);
    gu_mutex_init (&conn->recv_lock, NULL);
/* REMOVE
    gu_mutex_init (&conn->lock, NULL);
    gu_mutex_init (&conn->prim_lock, NULL);

    pthread_cond_init  (&conn->prim_cond, NULL);
*/
    conn->recv_acts_num = 0;
    conn->recv_acts = NULL;
    /* receive primary configuration message to get our id and stuff.
     * This will have to be removed in order to pass the RPIM event to the
     * application. We don't need to wait for anything here. */
    {
	gcs_seqno_t     act_id;
	gcs_act_type_t  act_type;
	uint8_t         *action;
	int ret;
	do {
	    ret = gcs_generic_recv
		(conn, &action, &act_type, &act_id);
	    if (ret < 0) return ret;
	    gu_debug ("Received action: act_id = %llu, act_type = %d, "
                      "act_size = %d, action = %p",
                      act_id, act_type, ret, action);
	    if (action) free (action); // hide action from upper layer
	} while (act_type != GCS_ACT_PRIMARY);
    }

    assert (conn->recv_acts_num);
    assert (conn->recv_acts);

    conn->proto_ver = 0;
    
    *generic = conn;
    return 0;

out1:
    gu_free (conn->recv_msg.buf);
out:
    conn->backend.close (&conn->backend);
    gu_free (conn);
    *generic = NULL;
    return err;
}

ssize_t
gcs_generic_send (gcs_generic_conn_t* const conn,
		  const uint8_t*            action,
		  size_t                    act_size,
		  gcs_act_type_t      const act_type)
{
    ssize_t        ret  = 0;
    size_t         sent = 0;
    gcs_act_frag_t frg;
    size_t         head_size;
    size_t         send_size;

    if (conn->state != GENERIC_PRIMARY)           return -ENOTCONN;
    if ((ret = gu_mutex_lock (&conn->send_lock))) return -ret;

    /* 
     * Action header will be replicated with every message.
     * It may seem like an extra overhead, but it is tiny
     * so far and simplifies A LOT.
     */

    /* Initialize action constants */
    frg.act_size  = act_size;
    frg.act_type  = act_type;
    frg.act_id    = conn->send_act_no; /* increment for every new action */
    frg.frag_no   = 0;
    frg.proto_ver = conn->proto_ver;

    if ((ret = gcs_act_proto_write (&frg, conn->send_buf, conn->send_buf_len)))
	goto out;

    head_size = frg.frag - conn->send_buf; 

    if ((ret = gcs_fifo_put (conn->fifo, action)))
	goto out;

    do {
	const size_t chunk_size =
	    act_size < frg.frag_len ? act_size : frg.frag_len;

	/* Here is the only time we have to cast frg.frag */
	memcpy ((char*)frg.frag, action, chunk_size);
	
	send_size = head_size + chunk_size;
//        assert(send_size < 1346);
	while ((ret = conn->backend.send (&conn->backend,
					  conn->send_buf,
					  send_size,
					  GCS_MSG_ACTION))
	       == -EAGAIN) {
	    /* wait for primary configuration - sleep 0.01 sec */
	    gu_debug ("Backend requested wait\n");
	    usleep (10000);
	}

	if (ret < head_size) { 
	    // we managed to send less than a header, fail
            gu_error ("Cannot send message: header is too big");
	    goto out1;
	}

	assert (ret <= send_size);

	ret -= head_size;

	sent     += ret;
	action   += ret;
	act_size -= ret;

	if (ret > 0) frg.frag_len = ret; // don't copy more than we can send

    } while (act_size && gcs_act_proto_inc(conn->send_buf));

    assert (0 == act_size);

    /* successfully sent action, increment send counter */
    conn->send_act_no++;

out1:
    /* At this point we can have unsent action in local FIFO
     * and parts of this action already could have been received
     * by other group members.
     * This should be only due to NON_PRIM configuration.
     * Action must be popped up from fifo. Members will have to discard received
     * fragments. FIXME.
     */
out:
    gu_mutex_unlock (&conn->send_lock);
    return ret;
}

/* A helper for gcs_generic_recv().
 * Deals with fetching complete message from backend
 * and reallocates recv buf if needed */
static inline long
generic_msg_recv (gcs_backend_t* backend, generic_recv_msg_t* recv_msg)
{
    long ret;

    ret = backend->recv (backend,
			 recv_msg->buf,
			 recv_msg->buf_len,
			 &recv_msg->type,
			 &recv_msg->sender_id);

    while (ret > recv_msg->buf_len) {
	/* recv_buf too small, reallocate */
        /* sometimes - like in case of component message, we may need to
         * do reallocation 2 times. This should be fixed in backend */
	void* msg = gu_realloc (recv_msg->buf, ret);
	gu_debug ("Reallocating buffer from %d to %d bytes",
		  recv_msg->buf_len, ret);
	if (msg) {
	    /* try again */
	    recv_msg->buf     = msg;
	    recv_msg->buf_len = ret;

	    ret = backend->recv (backend,
				 recv_msg->buf,
				 recv_msg->buf_len,
				 &recv_msg->type,
				 &recv_msg->sender_id);

	    /* should be either an error or an exact match */
	    assert ((ret < 0) || (ret >= recv_msg->buf_len));
	}
	else {
	    /* realloc unsuccessfull, old recv_buf remains */
	    gu_error ("Failed to reallocate buffer to %d bytes", ret);
	    ret = -ENOMEM;
            break;
	}
    }

    if (ret >= 0) {
	recv_msg->size = ret;
    }
    else {
	gu_debug ("returning %d: %s\n", ret, gcs_strerror (ret));
    }
    return ret;
}

/*
 *  Allocates memory and performs defragmentation for foreign actions.
 *  Keeps track of local actions.
 *  Returns 1 in case full action is received, negative error code otherwise
 */
static inline long
generic_handle_action_msg (generic_recv_act_t*       act,
			   const generic_recv_msg_t* msg,
			   bool                      foreign)
{
    long           ret;
    gcs_act_frag_t frg;

    if ((ret = gcs_act_proto_read (&frg, msg->buf, msg->size)))
	return ret;

    if (!act->received) {
	/* new action */
	assert (0 == frg.frag_no);
	act->size    = frg.act_size;
	act->send_no = frg.act_id;
	act->type    = frg.act_type;
	if (foreign) {
	    /* A foreign action. We need to allocate buffer for it.
             * This buffer will be returned to application,
	     * so it must be allocated by standard malloc */
	    if(!(act->head = malloc (act->size))) return -ENOMEM;
	    act->tail = act->head;
	}
    }
    else {
	/* another fragment of existing action */
        assert (frg.frag_no  >  0);
	assert (act->send_no == frg.act_id);
	assert (act->type    == frg.act_type);
    }
    
    act->received += frg.frag_len;
    assert (act->received <= act->size);

    if (foreign) {
	assert (act->tail);
	memcpy (act->tail, frg.frag, frg.frag_len);
	act->tail += frg.frag_len;
    }

    return (act->received == act->size);
}

/*
 * This function returns pointer to received action and reinitializes
 * recv_act_t structure.
 * It is called in the context of the last received message, which
 * is held in conn->recv_msg, so conn is the only needed parameter.
 */
static inline uint8_t*
generic_pop_action (gcs_generic_conn_t* conn,
		    generic_recv_act_t* act,
		    long sender_id)
{
    uint8_t* ret;

    assert (act->size == act->received);

    if (conn->my_id != sender_id) {
	ret = act->head;
    }
    else { /* local action, get from FIFO */
	ret = gcs_fifo_get (conn->fifo);
    }
    
    memset (act, 0, sizeof (*act));
    act->send_no = GCS_SEQNO_ILL;

    return ret;
}

/*!
 * Handles component message - installs new membership,
 * cleans old one, renews process id and signals waiting send threads
 * to continue.
 *
 * @return
 *        GENERIC_PRIMARY or GENERIC_NON_PRIMARY in case of success or
 *        negative error code.
 */
static long
generic_handle_component (gcs_generic_conn_t* conn,
			  gcs_comp_msg_t* comp)
{
    long recv_acts_num = 0;
    long new_idx, old_idx;
    generic_recv_act_t *recv_acts = NULL;

    gu_debug ("primary = %s, my_id = %d, memb_num = %d, conn_state = %d",
	      gcs_comp_msg_primary(comp) ? "yes" : "no",
	      gcs_comp_msg_self(comp), gcs_comp_msg_num (comp),
              conn->state);

    if (gcs_comp_msg_primary(comp)) {
	/* Got PRIMARY COMPONENT - Hooray! */
	/* create new recv_acts array according to new membrship */
	recv_acts_num = gcs_comp_msg_num(comp);
	recv_acts     = GU_CALLOC (recv_acts_num, generic_recv_act_t);
	if (!recv_acts) return -ENOMEM;
	
	if (conn->state == GENERIC_PRIMARY) {
	    /* we come from previous primary configuration */
	    /* remap old array to new one to preserve action continuity */
	    assert (conn->comp);
	    assert (conn->recv_acts);
	    gu_debug ("\tMembers:");
	    for (new_idx = 0; new_idx < recv_acts_num; new_idx++) {
		/* find member index in old component by unique member id */
		gu_debug ("\t%s", gcs_comp_msg_id (comp, new_idx));
		old_idx  = gcs_comp_msg_idx (conn->comp,
					     gcs_comp_msg_id (comp, new_idx));
		if (old_idx >= 0) {
		    /* the node was in previous configuration with us */
		    assert (old_idx < conn->recv_acts_num);
		    /* move recv buf to new recv array */
		    recv_acts[new_idx] = conn->recv_acts[old_idx];
		    conn->recv_acts[old_idx].head = NULL;
		}
	    }
	}
	else {
	    /* It happened so that we missed some primary configurations */
	    gu_debug ("Discontinuity in primary configurations!");
	    gu_debug ("State snapshot is needed!");
	    conn->state = GENERIC_PRIMARY;
	}
    }
    else {
	/* Got NON-PRIMARY COMPONENT - cleanup */
	if (conn->state == GENERIC_PRIMARY) {
	    /* All sending threads must be aborted with -ENOTCONN,
	     * local action FIFO must be flushed. Not implemented: FIXME! */
	    conn->state = GENERIC_NON_PRIMARY;
	}
    }

    /* free actions that were left from disappeared members */
    for (old_idx = 0; old_idx < conn->recv_acts_num; old_idx++) {
        if (conn->recv_acts[old_idx].head) {
	    // was alloced by normal malloc
            free (conn->recv_acts[old_idx].head);
        }
    }

    /* replace old component data with new one
     * (recv_acts is null when first primary configuration comes) */
    if (conn->recv_acts) gu_free (conn->recv_acts);
    if (conn->comp)      gcs_comp_msg_delete (conn->comp);
    conn->recv_acts     = recv_acts;
    conn->recv_acts_num = recv_acts_num;
    conn->comp          = gcs_comp_msg_copy (comp);
    conn->my_id         = gcs_comp_msg_self (comp);

    return conn->state;
}


//static void recv_lock_cleanup (void *m) { gu_mutex_unlock (m); }

/*! Receives action */
ssize_t gcs_generic_recv (gcs_generic_conn_t* conn,
			  uint8_t**           action,
			  gcs_act_type_t*     act_type,
			  gcs_seqno_t*        act_id) // should not be here!!!
{
    generic_recv_msg_t* recv_msg = &conn->recv_msg;
    long ret = 0;

    *action   = NULL;
    *act_type = GCS_ACT_ERROR;

    if (gu_mutex_lock (&conn->recv_lock)) return -EBADFD;
    if (GENERIC_CLOSED == conn->state) {
	gu_mutex_unlock (&conn->recv_lock);
	return -EBADFD;
    }

    /* receive messages from group and demultiplex them 
     * until finally some complete action is ready */
    while (1)
    {
	long sender_id;
	ret = generic_msg_recv (&conn->backend, recv_msg);
	if (ret < 0) goto out; /* backend error while receiving message */
	sender_id = recv_msg->sender_id;

	switch (recv_msg->type) {
	case GCS_MSG_ACTION:
            assert (sender_id >= 0);
	    if (GENERIC_PRIMARY == conn->state) {
		generic_recv_act_t* recv_act  = conn->recv_acts + sender_id;

		ret = generic_handle_action_msg (recv_act,
						 recv_msg,
						 sender_id != conn->my_id);
		if (ret == 1) {
		    /* complete action received */
		    *act_id   = ++conn->recv_act_no;
		    *act_type = recv_act->type;
		    ret       = recv_act->size;
		    *action   = generic_pop_action (conn, recv_act, sender_id);
//                   gu_debug ("Received action: sender: %d, size: %d, act: %p",
//                              conn->recv_msg.sender_id, ret, *action);
//                    gu_debug ("%s", (char*) *action);
		    goto out; /* exit loop */
		}
		else if (ret < 0) {
		    goto out;
		}
	    }
	    else { /* Non-primary - ignore action */
		gu_debug ("Action message in non-primary configuration from "
			  "member %d (%s)",
			  sender_id, gcs_comp_msg_id(conn->comp, sender_id));
	    }
	    break;
	case GCS_MSG_COMPONENT:
	    *action = NULL;
	    if ((ret = generic_handle_component (conn, recv_msg->buf)) >= 0) {
		assert (ret == conn->state);
		if (GENERIC_PRIMARY == ret) {
		    *act_type = GCS_ACT_PRIMARY;
                    *act_id   = conn->recv_act_no;
		} else {
		    *act_type = GCS_ACT_NON_PRIMARY;
                    //don't increment according to spec
                    *act_id   = conn->recv_act_no;
		}
		gu_info ("Received %s component event.",
			 GENERIC_PRIMARY == ret ? "primary" : "non-primary");
		ret = 0; // return size
	    }
	    else {
		*act_id   = GCS_SEQNO_ILL;
		gu_fatal ("Failed to handle component event: '%s'!",
			  strerror (ret));
	    }
	    goto out;
	default:
	    // this normaly should not happen, shall we bother with
	    // protection?
	    gu_warn ("Received unsupported message type: %d, length: %d, "
                     "sender: %d",
		     recv_msg->type, recv_msg->size, recv_msg->sender_id);
	    // continue looping
        }
    } /* end of recv loop */

out:
    gu_mutex_unlock (&conn->recv_lock);
    return ret;
}

long gcs_generic_close (gcs_generic_conn_t** generic)
{
    gcs_generic_conn_t *conn = *generic;
    uint8_t* dead_action = NULL;
    int i = 0;

    if (!conn) return -EINVAL;
    if (gu_mutex_lock (&conn->send_lock)) return -EBADFD;
    if (GENERIC_CLOSED == conn->state) {
	gu_mutex_unlock (&conn->send_lock);
	return -EBADFD;
    }
    conn->state = GENERIC_CLOSED;
    gu_mutex_unlock (&conn->send_lock);
    /* at this point all send attempts are isolated */

    conn->backend.close (&conn->backend);
    /* after that receiving thread should abort,
     * we can sync with recv thread */
    gu_mutex_lock   (&conn->recv_lock);
    gu_mutex_unlock (&conn->recv_lock);

    /* after that we must be able to destroy mutexes */
    while (gu_mutex_destroy (&conn->send_lock));
    while (gu_mutex_destroy (&conn->recv_lock));
    /* now noone will interfere */

    gcs_fifo_destroy (&conn->fifo);

    /* clean up non-received actions */
    for (i = 0; i < conn->recv_acts_num; i++) {
	dead_action = conn->recv_acts[i].head;
	if (dead_action) gu_free (dead_action);
    }
    gu_free (conn->recv_acts);

    /* free buffers */
    gu_free (conn->recv_msg.buf);
    gu_free (conn->send_buf);
    gu_free (conn);
    *generic = NULL;
    return 0;
}

long
gcs_generic_set_pkt_size (gcs_generic_conn_t* conn, ulong pkt_size)
{
    long msg_size = conn->backend.msg_size (&conn->backend, pkt_size);
    long hdr_size = gcs_act_proto_hdr_size (conn->proto_ver);
    uint8_t* new_send_buf = NULL;

    if (hdr_size < 0) return hdr_size;

    if (msg_size <= hdr_size) {
        gu_warn ("Requested packet size %d is too small, "
                 "using smallest possible: %d",
                 pkt_size, pkt_size + (hdr_size - msg_size + 1));
        msg_size = hdr_size + 1;
    }

    gu_debug ("Changing maximum message size %u -> %u",
              conn->send_buf_len, msg_size);

    if (gu_mutex_lock (&conn->send_lock)) return -EBADFD;
    if (GENERIC_CLOSED != conn->state) {
        new_send_buf = gu_realloc (conn->send_buf, msg_size);
        if (new_send_buf) {
            conn->send_buf     = new_send_buf;
            conn->send_buf_len = msg_size;
        }
    }
    gu_mutex_unlock (&conn->send_lock);

    if (new_send_buf)
        return 0;
    else
        return -ENOMEM;
}
