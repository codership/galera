/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Implementation of the generic communication layer.
 * See gcs_core.h 
 */

#include <string.h> // for mempcpy
#include <errno.h>

#include <galerautils.h>

#define GCS_FIFO_SAFE

#include "gcs_backend.h"
#include "gcs_comp_msg.h"
#include "gcs_fifo.h"
#include "gcs_group.h"
#include "gcs_core.h"

const size_t CORE_FIFO_LEN = 1<<15; // 32K elements (128/256K bytes)
const size_t CORE_INIT_BUF_SIZE = 4096;

typedef enum core_state
{
    CORE_PRIMARY,
    CORE_NON_PRIMARY,
    CORE_CLOSED,
    CORE_DESTROYED
}
core_state_t;

struct gcs_core
{
    /* connection per se */
    long            prim_comp_no;
    core_state_t    state;

    /* protocol */
    long            proto_ver;

    /* send part */
    gu_mutex_t      send_lock;
    bool            send_restart; // action send must be restarted.
    void*           send_buf;
    size_t          send_buf_len;
    gcs_seqno_t     send_act_no;

    /* recv part */
    gcs_recv_msg_t  recv_msg;
    gcs_seqno_t     recv_act_no;

    /* local action FIFO */
    gcs_fifo_t*     fifo;

    /* group context */
    gcs_group_t     group;

    /* backend part */
    size_t          msg_size;
    gcs_backend_t   backend;         // message IO context
};

gcs_core_t*
gcs_core_create (const char* const backend_uri)
{
    long        err  = 0;
    gcs_core_t* core = GU_CALLOC (1, gcs_core_t);

    if (NULL != core) {

        gu_debug ("Initializing backend IO layer");
        err = gcs_backend_init (&core->backend, backend_uri);
        if (0 == err) {
            assert (NULL != core->backend.conn);

            // Need to allocate something, otherwise Spread 3.17.3 freaks out.
            core->recv_msg.buf = gu_malloc(CORE_INIT_BUF_SIZE);
            if (core->recv_msg.buf) {
                core->recv_msg.buf_len = CORE_INIT_BUF_SIZE;

                core->fifo = GCS_FIFO_CREATE (CORE_FIFO_LEN);
                if (core->fifo) {
                    gu_mutex_init (&core->send_lock, NULL);
                    gcs_group_init (&core->group);
                    core->proto_ver = 0;
                    core->state = CORE_CLOSED;
                    return core; // success
                }

                gu_free (core->recv_msg.buf);
            }
            core->backend.destroy (&core->backend);
        }
        else {
            gu_error ("Failed to initialize backend: %d (%s)",
                      err, strerror(-err));
            gu_error ("Arguments: %p, %s",
                      &core->backend, backend_uri);
        }
        gu_free (core);
    }

    return NULL; // failure
}

long
gcs_core_open (gcs_core_t* core,
               const char* const channel)
{
    long ret;

    if (core->state != CORE_CLOSED) {
        gu_debug ("gcs_core->state isn't CLOSED: %d", core->state);
        return -EBADFD;
    }

    ret = core->backend.open (&core->backend, channel);
    core->state = CORE_NON_PRIMARY;

    if (0 == ret) {
	gcs_seqno_t     act_id;
	gcs_act_type_t  act_type;
	uint8_t         *action = NULL;
	int ret;

        /* receive primary configuration message to get our id and stuff.
         * This will have to be removed in order to pass the PRIM event to the
         * application. We don't need to wait for anything here. */
	do {
	    if (action) free (action); // clear previous action

	    ret = gcs_core_recv (core, &action, &act_type, &act_id);
	    if (ret < 0) return ret;

	    gu_debug ("Received action: act_id = %llu, act_type = %d, "
                      "act_size = %d, action = %p",
                      act_id, act_type, ret, action);

	} while (act_type != GCS_ACT_PRIMARY);

        core->state = CORE_PRIMARY;

        // In future at this point primary configuration action
        // might be passed to upper layer in order to initialize seqno
        free (action);
    }
    else {
        gu_error ("Failed to open backend connection: %d (%s)",
                  ret, strerror(-ret));
    }

    return ret;
}

/*!
 * Performs an attempt at sending a message (action fragment) with all
 * required checks while holding a lock, ensuring exclusive access to backend.
 *
 * restart flag may be raised if configuration changes and new nodes are
 * added - that would require all previous members to resend partially sent
 * actions.
 */
static inline ssize_t
core_msg_send (gcs_core_t*    core,
               const void*    buf,
               size_t         buf_len,
               gcs_msg_type_t type)
{
    ssize_t ret;

    ret = gu_mutex_lock (&core->send_lock);
    if (gu_likely(0 == ret)) {
        // TODO: nothing here can tell between GCS_ACT_DATA and GCS_ACT_SERVICE
        // when GCS_ACT_SERVICE will be implemented, we'll have to have two
        // restart flags - one for each of ongoing actions
        register bool restart = core->send_restart && (type == GCS_MSG_ACTION);
        if (gu_likely(core->state == CORE_PRIMARY && !restart)) {
            ret = core->backend.send (&core->backend, buf, buf_len, type);
        }
        else {
            if (core->state == CORE_PRIMARY && restart) {
                core->send_restart = false; // next action doesn't need it
                ret = -ERESTART;            // ask to restart action sending
            }
            else {
                switch (core->state) {
                case CORE_NON_PRIMARY: ret = -ENOTCONN; break;
                case CORE_CLOSED:      ret = -ECONNABORTED; break;
                case CORE_DESTROYED:   ret = -EBADFD; break;
                default: assert(0);
                }
            }
        }
        gu_mutex_unlock (&core->send_lock);
    }
//    gu_debug ("returning: %d (%s)", ret, strerror(-ret));
    return ret;
}

/*!
 * Repeats attempt at sending the message if -EAGAIN was returned
 * by core_msg_send()
 */
static inline ssize_t
core_msg_send_retry (gcs_core_t*    core,
                     const void*    buf,
                     size_t         buf_len,
                     gcs_msg_type_t type)
{
    ssize_t ret;
    while ((ret = core_msg_send (core, buf, buf_len, type)) == -EAGAIN) {
        /* wait for primary configuration - sleep 0.01 sec */
        gu_debug ("Backend requested wait\n");
        usleep (10000);
    }
//    gu_debug ("returning: %d (%s)", ret, strerror(-ret));
    return ret;
}

ssize_t
gcs_core_send (gcs_core_t*      const conn,
               const uint8_t*         action,
               size_t                 act_size,
               gcs_act_type_t   const act_type)
{
    ssize_t        ret  = 0;
    size_t         sent = 0;
    gcs_act_frag_t frg;
    size_t         send_size;
    const unsigned char proto_ver = conn->proto_ver;
    const size_t   hdr_size  = gcs_act_proto_hdr_size (proto_ver);


    /* 
     * Action header will be replicated with every message.
     * It may seem like an extra overhead, but it is tiny
     * so far and simplifies A LOT.
     */

    /* Initialize action constants */
    frg.act_size  = act_size;
    frg.act_type  = act_type;
    frg.act_id    = conn->send_act_no; /* incremented for every new action */
    frg.frag_no   = 0;
    frg.proto_ver = proto_ver;

    if ((ret = gcs_act_proto_write (&frg, conn->send_buf, conn->send_buf_len)))
	goto out;

    if ((ret = GCS_FIFO_PUT (conn->fifo, action)))
	goto out;

    do {
	const size_t chunk_size =
	    act_size < frg.frag_len ? act_size : frg.frag_len;

	/* Here is the only time we have to cast frg.frag */
	memcpy ((char*)frg.frag, action, chunk_size);
	
	send_size = hdr_size + chunk_size;

        ret = core_msg_send_retry (conn, conn->send_buf, send_size,
                                   GCS_MSG_ACTION);

	if (gu_likely(ret > (ssize_t)hdr_size)) {

            assert (ret <= (ssize_t)send_size);

            ret -= hdr_size;

            sent     += ret;
            action   += ret;
            act_size -= ret;

            // adjust frag_len, don't copy more than we could send
            frg.frag_len = ret;
        }
        else {
            if (ret >= 0) {
                // we managed to send less than a header, fail
                gu_fatal ("Cannot send message: header is too big");
                ret = -ENOTRECOVERABLE;
            }
            /* At this point we have an unsent action in local FIFO
             * and parts of this action already could have been received
             * by other group members.
             * 1. Action must be removed from fifo.*/
            GCS_FIFO_REMOVE (conn->fifo);
            /* 2. Members will have to discard received fragments.
             * Two reasons could lead us here: new member(s) in configuration
             * change or broken connection (leave group). In both cases other
             * members discard fragments */
            goto out;
	}

    } while (act_size && gcs_act_proto_inc(conn->send_buf));

    assert (0 == act_size);

    /* successfully sent action, increment send counter */
    conn->send_act_no++;
    ret = sent;

out:
//    gu_debug ("returning: %d (%s)", ret, strerror(-ret));
    return ret;
}

/* A helper for gcs_core_recv().
 * Deals with fetching complete message from backend
 * and reallocates recv buf if needed */
static inline long
core_msg_recv (gcs_backend_t* backend, gcs_recv_msg_t* recv_msg)
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
	gu_debug ("returning %d: %s\n", ret, strerror(-ret));
    }
    return ret;
}

/*! Receives action */
ssize_t gcs_core_recv (gcs_core_t*      conn,
                       uint8_t**        action,
                       gcs_act_type_t*  act_type,
                       gcs_seqno_t*     act_id) // global ID
{
    gcs_recv_msg_t* recv_msg = &conn->recv_msg;
    gcs_group_t*    group    = &conn->group;
    long ret = 0;

    *action   = NULL;
    *act_type = GCS_ACT_ERROR;
    *act_id   = GCS_SEQNO_ILL; // by default action is unordered

    if (gu_mutex_lock (&conn->send_lock)) return -EBADFD;
    if (conn->state >= CORE_CLOSED) {
	gu_mutex_unlock (&conn->send_lock);
	return -EBADFD;
    }
    gu_mutex_unlock (&conn->send_lock);

    /* receive messages from group and demultiplex them 
     * until finally some complete action is ready */
    while (1)
    {
	ret = core_msg_recv (&conn->backend, recv_msg);
	if (gu_unlikely (ret < 0)) {
            goto out; /* backend error while receiving message */
        }

	switch (recv_msg->type) {
	case GCS_MSG_ACTION:
            if (gcs_group_is_primary(group)) {
		gcs_recv_act_t recv_act;

		ret = gcs_group_handle_act_msg (group, recv_msg, &recv_act);
		if (ret > 0) {
		    /* complete action received */
		    *act_id   = ++conn->recv_act_no;
		    *act_type = recv_act.type;
                    if (gu_likely(recv_act.buf != NULL)) {
                        assert (gcs_group_my_idx(group) != recv_act.sender_id);
                        *action = recv_act.buf;
                    }
                    else {
                        assert (gcs_group_my_idx(group) == recv_act.sender_id);
                        /* local action, get from FIFO */
                        *action = GCS_FIFO_GET (conn->fifo);
                        assert (NULL != *action);
                    }
//                   gu_debug ("Received action: sender: %d, size: %d, act: %p",
//                              conn->recv_msg.sender_id, ret, *action);
//                   gu_debug ("%s", (char*) *action);
		    goto out; /* exit loop */
		}
		else if (ret < 0) {
                    assert (0);
		    goto out;
		}
	    }
	    else { /* Non-primary - ignore action */
		gu_warn ("Action message in non-primary configuration from "
			  "member %d", recv_msg->sender_id);
	    }
	    break;
        case GCS_MSG_FLOW:
            *act_type = GCS_ACT_FLOW;
            *action   = recv_msg->buf; // no need to malloc since it is internal
        case GCS_MSG_LAST:
            if (gcs_group_is_primary(group)) {
                gcs_seqno_t commit_cut =
                    gcs_group_handle_last_msg (group, recv_msg);
                if (commit_cut) {
                    /* commit cut changed */
                    if ((*action  = malloc (sizeof (commit_cut)))) {
// remove                       *act_id   = ++conn->recv_act_no;
                        *act_type = GCS_ACT_COMMIT_CUT;
                        *((gcs_seqno_t*)*action) = commit_cut;
                        ret = sizeof(commit_cut);
                        goto out;
                    }
                    else {
                        gu_fatal ("Out of memory for GCS_ACT_COMMIT_CUT");
                        abort();
                    }
                }
	    }
	    else { /* Non-primary - ignore last message */
		gu_warn ("Last Applied Action message "
                         "in non-primary configuration from member %d",
                         recv_msg->sender_id);
	    }
            break;
	case GCS_MSG_COMPONENT:
	    *action = NULL;
            ret = gcs_group_handle_comp_msg (group, recv_msg->buf);
	    if (ret >= 0) {
                gu_mutex_lock (&conn->send_lock);
                {
                    if (gcs_group_is_primary (group)) {
                        *act_type = GCS_ACT_PRIMARY;
// remove                       *act_id   = conn->recv_act_no;
                        // if new members are added, need to resend ongoing
                        // action
                        conn->send_restart = gcs_group_new_members (group);
                        // if state is CLOSING or CLOSED we don't change that
                        if (conn->state == CORE_NON_PRIMARY)
                            conn->state = CORE_PRIMARY;
                    } else {
                        *act_type = GCS_ACT_NON_PRIMARY;
                        //don't increment according to spec
// remove                       *act_id   = conn->recv_act_no;
                        if (conn->state == CORE_PRIMARY)
                            conn->state = CORE_NON_PRIMARY;
                    }
                    gu_info ("Received %s component event.",
                             gcs_group_is_primary(group) ?
                             "primary" : "non-primary");
                    ret = 0; // return size
                }
                gu_mutex_unlock (&conn->send_lock);
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
#ifdef GCS_DEBUG_CORE
    if (ret < 0)
        gu_debug ("Returning %d", ret);
#endif
    return ret;
}

long gcs_core_close (gcs_core_t* conn)
{
    long ret;

    if (!conn) return -EBADFD;
    if (gu_mutex_lock (&conn->send_lock)) return -EBADFD;
    if (conn->state >= CORE_CLOSED) {
	gu_mutex_unlock (&conn->send_lock);
	return -EBADFD;
    }

    conn->state = CORE_CLOSED;
    ret = conn->backend.close (&conn->backend);
    gu_mutex_unlock (&conn->send_lock);

    return ret;
}

long gcs_core_destroy (gcs_core_t* core)
{
    long ret;

    if (!core) return -EBADFD;

    if (gu_mutex_lock (&core->send_lock)) return -EBADFD;
    {
        if (CORE_CLOSED != core->state) {
            if (core->state < CORE_CLOSED)
                gu_error ("Calling destroy() before close().");
            gu_mutex_unlock (&core->send_lock);
            return -EBADFD;
        }
        core->state = CORE_DESTROYED;
    }
    gu_mutex_unlock (&core->send_lock);
    /* at this point all send attempts are isolated */

    /* after that we must be able to destroy mutexes */
    while (gu_mutex_destroy (&core->send_lock));

    /* now noone will interfere */
    GCS_FIFO_DESTROY (&core->fifo);
    gcs_group_free (&core->group);

    /* free buffers */
    gu_free (core->recv_msg.buf);
    gu_free (core->send_buf);

    ret = core->backend.destroy (&core->backend);

    gu_free (core);

    return ret;
}

long
gcs_core_set_pkt_size (gcs_core_t* conn, ulong pkt_size)
{
    long msg_size = conn->backend.msg_size (&conn->backend, pkt_size);
    long hdr_size = gcs_act_proto_hdr_size (conn->proto_ver);
    uint8_t* new_send_buf = NULL;
    long ret = 0;

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
    {
        if (conn->state != CORE_DESTROYED) {
            new_send_buf = gu_realloc (conn->send_buf, msg_size);
            if (new_send_buf) {
                conn->send_buf     = new_send_buf;
                conn->send_buf_len = msg_size;
            }
            else {
                ret = -ENOMEM;
            }
        }
        else {
            ret =  -EBADFD;
        }
    }
    gu_mutex_unlock (&conn->send_lock);

    return ret;
}

long
gcs_core_set_last_applied (gcs_core_t* core, gcs_seqno_t seqno)
{
    ssize_t ret;
    seqno = gcs_seqno_le (seqno);
    ret = core_msg_send_retry (core, &seqno, sizeof(seqno), GCS_MSG_LAST);
    if (ret > 0) {
        assert(ret == sizeof(seqno));
        ret = 0;
    }
    return ret;
}
