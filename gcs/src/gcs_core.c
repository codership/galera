/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 *
 *
 * Implementation of the generic communication layer.
 * See gcs_core.h 
 */

#include <string.h> // for mempcpy
#include <errno.h>

#include <galerautils.h>

#include "gcs_backend.h"
#include "gcs_comp_msg.h"
#include "gcs_fifo_lite.h"
#include "gcs_group.h"
#include "gcs_core.h"

const size_t CORE_FIFO_LEN = 1 << 8; // 256 elements (no need to have more)
const size_t CORE_INIT_BUF_SIZE = 4096;

typedef enum core_state
{
    CORE_PRIMARY,
    CORE_EXCHANGE,
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

    /* local action FIFO */
    gcs_fifo_lite_t*     fifo;

    /* group context */
    gcs_group_t     group;

    /* backend part */
    size_t          msg_size;
    gcs_backend_t   backend;         // message IO context
};

// this is to pass local action info from send to recv thread.
typedef struct core_act
{
    gcs_seqno_t sent_act_id;
    const void* action;
}
core_act_t;

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

                core->fifo = gcs_fifo_lite_create (CORE_FIFO_LEN,
                                                   sizeof (core_act_t));
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
gcs_core_init (gcs_core_t* core, gcs_seqno_t seqno, const gu_uuid_t* uuid)
{
    if (core->state == CORE_CLOSED) {
        return gcs_group_init_history (&core->group, seqno, uuid);
    }
    else {
        gu_error ("State must be CLOSED");
        if (core->state < CORE_CLOSED)
            return -EBUSY;
        else // DESTROYED
            return -EBADFD;
    }
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
    if (!ret) {
        core->state = CORE_NON_PRIMARY;
    }
    else {
        gu_error ("Failed to open backend connection: %d (%s)",
                  ret, strerror(-ret));
    }

    return ret;
}

/* Translates different core states into standard errors */
static ssize_t
core_error (core_state_t state)
{
    switch (state) {
    case CORE_NON_PRIMARY: return -ENOTCONN;
    case CORE_CLOSED:      return -ECONNABORTED;
    case CORE_DESTROYED:   return -EBADFD;
    default: assert(0);    return -ENOTRECOVERABLE;
    }
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
        if (gu_likely((core->state == CORE_PRIMARY  && !restart) ||
                      (core->state == CORE_EXCHANGE && type == GCS_MSG_STATE_MSG))) {
            ret = core->backend.send (&core->backend, buf, buf_len, type);
        }
        else {
            if (core->state == CORE_PRIMARY && restart) {
                core->send_restart = false; // next action doesn't need it
                ret = -ERESTART;            // ask to restart action sending
            }
            else {
                ret = core_error (core->state);
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
               const void*            action,
               size_t                 act_size,
               gcs_act_type_t   const act_type)
{
    ssize_t        ret  = 0;
    size_t         sent = 0;
    gcs_act_frag_t frg;
    size_t         send_size;
    const unsigned char proto_ver = conn->proto_ver;
    const size_t   hdr_size  = gcs_act_proto_hdr_size (proto_ver);

    core_act_t*     local_act;

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

    if ((local_act = gcs_fifo_lite_get_tail (conn->fifo))) {
        *local_act  = (typeof(*local_act)){ conn->send_act_no, action };
        gcs_fifo_lite_push_tail (conn->fifo);
    }
    else {
        ret = core_error (conn->state);
        gu_error ("Failed to access core FIFO: %d (%s)", ret, strerror (-ret));
	goto out;
    }

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
             * (first parts of action might be even received by this node,
             *  so that there is nothing to remove, but we cannot know for sure)
             *
             * 1. Action must be removed from fifo.*/
            gcs_fifo_lite_remove (conn->fifo);
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
			 &recv_msg->sender_idx);

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
				 &recv_msg->sender_idx);

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

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_ACTION.
 *
 * @return action size, negative error code or 0 to continue.
 */
static inline ssize_t
core_handle_act_msg (gcs_core_t*     core,
                     gcs_recv_msg_t* msg,
                     gcs_recv_act_t* act)
{
    ssize_t        ret = 0;
    gcs_group_t*   group = &core->group;
    gcs_act_frag_t frg;

    assert (GCS_MSG_ACTION == msg->type);

    if (gcs_group_is_primary(group)) {

        ret = gcs_act_proto_read (&frg, msg->buf, msg->size);
        if (gu_unlikely(ret)) {
            gu_fatal ("Error parsing action fragment header: %zd (%s).",
                      ret, strerror (-ret));
            assert (ret == 0);
            return -ENOTRECOVERABLE;
        }

        ret = gcs_group_handle_act_msg (group, &frg, msg, act);

        if (ret > 0) { /* complete action received */

            if (gu_likely(gcs_group_my_idx(group) != act->sender_idx)) {
                /* foreign action, must be passed from gcs_group */
                assert (act->buf != NULL);
            }
            else {
                /* local action, get from FIFO, should be there already */
                core_act_t* local_act;
                gcs_seqno_t sent_act_id;
                assert (act->buf == NULL);

                if ((local_act = gcs_fifo_lite_get_head (core->fifo))){
                    act->buf    = local_act->action;
                    sent_act_id = local_act->sent_act_id;
                    assert (NULL != act->buf);
                    gcs_fifo_lite_pop_head (core->fifo);
                    /* sanity check */
                    if (gu_unlikely(sent_act_id != frg.act_id)) {
                        gu_fatal ("FIFO violation: expected sent_act_id %lld "
                                  "found %lld", sent_act_id, frg.act_id);
                        ret = -ENOTRECOVERABLE;
                    }
                }
                else {
                    gu_fatal ("FIFO violation: queue empty when local action "
                              "received");
                    ret = -ENOTRECOVERABLE;
                }
            }
//          gu_debug ("Received action: seqno: %lld, sender: %d, size: %d, act: %p",
//                     act->id, msg->sender_idx, ret, act->buf);
//          gu_debug ("%s", (char*) act->buf);
        }
        else if (gu_unlikely(ret < 0)){
            gu_fatal ("Failed to handle action fragment: %zd (%s)",
                      ret, strerror(ret));
            assert (0);
            return -ENOTRECOVERABLE;
        }
    }
    else { /* Non-primary - ignore action */
        gu_warn ("Action message in non-primary configuration from "
                 "member %d", msg->sender_idx);
    }

    return ret;
}

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_LAST.
 *
 * @return action size, negative error code or 0 to continue.
 */
static ssize_t
core_handle_last_msg (gcs_core_t*     core,
                      gcs_recv_msg_t* msg,
                      gcs_recv_act_t* act)
{
    assert (GCS_MSG_LAST == msg->type);

    if (gcs_group_is_primary(&core->group)) {
        gcs_seqno_t commit_cut =
            gcs_group_handle_last_msg (&core->group, msg);
        if (commit_cut) {
            /* commit cut changed */
            if ((act->buf = malloc (sizeof (commit_cut)))) {
                act->type                 = GCS_ACT_COMMIT_CUT;
                *((gcs_seqno_t*)act->buf) = commit_cut;
                return sizeof(commit_cut);
            }
            else {
                gu_fatal ("Out of memory for GCS_ACT_COMMIT_CUT");
                return -ENOMEM;
            }
        }
    }
    else { /* Non-primary - ignore last message */
        gu_warn ("Last Applied Action message "
                 "in non-primary configuration from member %d",
                 msg->sender_idx);
    }
    return 0;
}

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_COMPONENT.
 *
 * @return action size, negative error code or 0 to continue.
 */
static ssize_t
core_handle_comp_msg (gcs_core_t*     core,
                      gcs_recv_msg_t* msg,
                      gcs_recv_act_t* act)
{
    ssize_t      ret = 0;
    gcs_group_t* group = &core->group;

    assert (GCS_MSG_COMPONENT == msg->type);

    ret = gcs_group_handle_comp_msg (group, msg->buf);

    switch (ret) {
    case GCS_GROUP_PRIMARY:
        /* New primary configuration. This happens if: 
         * - this is first node in group OR
         * - some nodes disappeared no new nodes appeared
         * No need for state exchange, return new conf_act right away */
        if (gu_mutex_lock (&core->send_lock)) abort();
        {
            assert (CORE_EXCHANGE != core->state);
            if (CORE_NON_PRIMARY == core->state) core->state = CORE_PRIMARY;
            core->send_restart = false;
        }
        gu_mutex_unlock (&core->send_lock);

        ret = gcs_group_act_conf (group, act);
        if (ret < 0) {
            gu_fatal ("Failed create PRIM CONF action: %d (%s)",
                      ret, strerror (-ret));
            assert (0);
            ret = -ENOTRECOVERABLE;
        }
        break;
    case GCS_GROUP_WAIT_STATE_UUID:
        /* New members, need state exchange. If representative, send UUID */
        if (gu_mutex_lock (&core->send_lock)) abort();
        {
            // if state is CLOSED or DESTROYED we don't do anything
            if (CORE_CLOSED > core->state) {
                if (0 == gcs_group_my_idx(group)) { // I'm representative
                    gu_uuid_t uuid;
                    gu_uuid_generate (&uuid, NULL, 0);
                    ret = core->backend.send (&core->backend,
                                              &uuid,
                                              sizeof(uuid),
                                              GCS_MSG_STATE_UUID);
                    if (ret < 0) {
                        // if send() failed, it means new configuration change 
                        // is on the way. Probably should ignore.
                        gu_warn ("Failed to send state UUID: %d (%s)",
                                 ret, strerror (-ret));
                    }
                    else {
                        gu_info ("STATE_EXCHANGE: sent state UUID: "
                                 GU_UUID_FORMAT, GU_UUID_ARGS(&uuid));
                    }
                }
                else {
                    gu_info ("STATE EXCHANGE: Waiting for state UUID.");
                }
                core->state = CORE_EXCHANGE;
                // since new members were added, need to resend ongoing actions
                core->send_restart = true;
            }
            ret = 0; // no action to return, continue
        }
        gu_mutex_unlock (&core->send_lock);
        break;
    case GCS_GROUP_NON_PRIMARY:
        /* Lost primary component */
        if (gu_mutex_lock (&core->send_lock)) abort();
        {
            // if state is CLOSING or CLOSED we don't change that
            if (CORE_PRIMARY  == core->state ||
                CORE_EXCHANGE == core->state) {
                    core->state = CORE_NON_PRIMARY;
            }
        }
        gu_mutex_unlock (&core->send_lock);

        ret = gcs_group_act_conf (group, act);
        if (ret < 0) {
            gu_fatal ("Failed create NON-PRIM CONF action: %d (%s)",
                      ret, strerror (-ret));
            assert (0);
            ret = -ENOTRECOVERABLE;
        }
        break;
    case GCS_GROUP_WAIT_STATE_MSG:
        gu_fatal ("Internal error: gcs_group_handle_comp() returned "
                  "WAIT_STATE_MSG. Can't continue.");
        ret = -ENOTRECOVERABLE;
        assert(0);
    default:
        gu_fatal ("Failed to handle component message: %d (%s)!",
                  ret, strerror (-ret));
        assert(0);
    }

    return ret;
}

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_STATE_UUID.
 *
 * @return negative error code or 0 to continue.
 */
static ssize_t
core_handle_uuid_msg (gcs_core_t*     core,
                      gcs_recv_msg_t* msg)
{
    ssize_t      ret   = 0;
    gcs_group_t* group = &core->group;

    assert (GCS_MSG_STATE_UUID == msg->type);

    if (GCS_GROUP_WAIT_STATE_UUID == gcs_group_state (group)) {

        ret = gcs_group_handle_uuid_msg (group, msg);

        switch (ret) {
        case GCS_GROUP_WAIT_STATE_MSG:
            // Need to send state message for state exchange
            {
                gcs_state_t* state = gcs_group_get_state (group);
                if (state) {
                    size_t           state_len = gcs_state_msg_len (state);
                    uint8_t          state_buf[state_len];
                    const gu_uuid_t* state_uuid = gcs_state_uuid (state);

                    gcs_state_msg_write (state_buf, state);
                    ret = core_msg_send_retry (core,
                                               state_buf,
                                               state_len,
                                               GCS_MSG_STATE_MSG);
                    if (ret > 0) {
                        gu_info ("STATE EXCHANGE: sent state msg: "
                                 GU_UUID_FORMAT, GU_UUID_ARGS(state_uuid));
                    }
                    else {
                        // This may happen if new configuraiton chage goes on.
                        // What shall we do in this case? Is it unrecoverable?
                        gu_error ("STATE EXCHANGE: failed for: "GU_UUID_FORMAT
                                 ": %d (%s)",
                                 GU_UUID_ARGS(state_uuid), ret, strerror(-ret));
                    }
                    gcs_state_destroy (state);
                }
                else {
                    gu_fatal ("Failed to allocate state object.");
                    ret = -ENOTRECOVERABLE;
                }
            }
            break;
        default:
            assert (ret < 0);
            gu_error ("Failed to handle state UUID: %d (%s)",
                      ret, strerror (-ret));
        }
    }

    return ret;
}

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_STATE_MSG.
 *
 * @return action size, negative error code or 0 to continue.
 */
static ssize_t
core_handle_state_msg (gcs_core_t*     core,
                       gcs_recv_msg_t* msg,
                       gcs_recv_act_t* act)
{
    ssize_t      ret = 0;
    gcs_group_t* group = &core->group;

    assert (GCS_MSG_STATE_MSG == msg->type);

    if (GCS_GROUP_WAIT_STATE_MSG == gcs_group_state (group)) {

        ret = gcs_group_handle_state_msg (group, msg);

        switch (ret) {
        case GCS_GROUP_PRIMARY:
        case GCS_GROUP_NON_PRIMARY:
            // state exchange is over, create configuration action
            if (gu_mutex_lock (&core->send_lock)) abort();
            {
                // if core is closing we do nothing    
                if (CORE_CLOSED > core->state) {
                    assert (CORE_EXCHANGE == core->state);
                    switch (ret) {
                    case GCS_GROUP_PRIMARY:
                        core->state = CORE_PRIMARY;
                        break;
                    case GCS_GROUP_NON_PRIMARY:
                        core->state = CORE_NON_PRIMARY;
                        break;
                    default:
                        assert (0);
                    }
                }
            }
            gu_mutex_unlock (&core->send_lock);

            ret = gcs_group_act_conf (group, act);
            if (ret < 0) {
                gu_fatal ("Failed create CONF action: %d (%s)",
                          ret, strerror (-ret));
                assert (0);
                ret = -ENOTRECOVERABLE;
            }
            break;
        case GCS_GROUP_WAIT_STATE_MSG:
            // waiting for more state messages
            ret = 0;
            break;
        default:
            assert (ret < 0);
            gu_error ("Failed to handle state message: %d (%s)",
                      ret, strerror (-ret));
        }
    }
    return ret;
}

/*!
 * Some service actions are for internal use and consist of a single message
 * (FLOW, JOIN, SYNC)
 * In this case we can simply use msg->buf as an action buffer, since we
 * can guarantee that we don't deallocate it. Action here is just a wrapper
 * to deliver message to the upper level.
 */
static ssize_t
core_msg_to_action (gcs_core_t*     core,
                    gcs_recv_msg_t* msg,
                    gcs_recv_act_t* act)
{
    ssize_t      ret = 0;
    gcs_group_t* group = &core->group;

    if (GCS_GROUP_PRIMARY == gcs_group_state (group)) {
        gcs_act_type_t act_type;

        switch (msg->type) {
        case GCS_MSG_FLOW: // most frequent
            ret = 1;
            act_type = GCS_ACT_FLOW;
            break;
        case GCS_MSG_JOIN:
            ret = gcs_group_handle_join_msg (group, msg);
            act_type = GCS_ACT_JOIN;
            break;
        case GCS_MSG_SYNC:
            ret = gcs_group_handle_sync_msg (group, msg);
            act_type = GCS_ACT_SYNC;
            break;
        default:
            gu_error ("Iternal error. Unexpected message type %s from ld%",
                      gcs_msg_type_string[msg->type], msg->sender_idx);
            assert (0);
            ret = -EPROTO;
        }

        if (ret > 0) {
            act->type = act_type;
            act->buf  = msg->buf;
            ret       = msg->size;
        }
    }
    else {
        gu_warn ("%s message from member %ld in non-primary configuration",
                 gcs_msg_type_string[msg->type], msg->sender_idx);
        assert(0);
    }

    return ret;
}

/*! Receives action */
ssize_t gcs_core_recv (gcs_core_t*      conn,
                       const void**     action,
                       gcs_act_type_t*  act_type,
                       gcs_seqno_t*     act_id) // global ID
{
    gcs_recv_act_t  recv_act;
    gcs_recv_msg_t* recv_msg = &conn->recv_msg;
    ssize_t         ret      = 0;

    recv_act.buf  = NULL;
    recv_act.type = GCS_ACT_ERROR;
    recv_act.id   = GCS_SEQNO_ILL; // by default action is unordered

    if (gu_mutex_lock (&conn->send_lock)) return -EBADFD;
    if (conn->state >= CORE_CLOSED) {
	gu_mutex_unlock (&conn->send_lock);
	return -EBADFD;
    }
    gu_mutex_unlock (&conn->send_lock);

    /* receive messages from group and demultiplex them 
     * until finally some complete action is ready */
    while (0 == ret)
    {
	ret = core_msg_recv (&conn->backend, recv_msg);
	if (gu_unlikely (ret < 0)) {
            goto out; /* backend error while receiving message */
        }

	switch (recv_msg->type) {
	case GCS_MSG_ACTION:
            ret = core_handle_act_msg(conn, recv_msg, &recv_act);
            assert (ret >= 0); // hang on error in debug mode
	    break;
        case GCS_MSG_LAST:
            ret = core_handle_last_msg(conn, recv_msg, &recv_act);
            assert (ret >= 0); // hang on error in debug mode
            break;
	case GCS_MSG_COMPONENT:
            ret = core_handle_comp_msg (conn, recv_msg, &recv_act);
            assert (ret >= 0); // hang on error in debug mode
            break;
        case GCS_MSG_STATE_UUID:
            ret = core_handle_uuid_msg (conn, recv_msg);
            assert (ret >= 0); // hang on error in debug mode
            ret = 0;           // continue waiting for state messages
            break;
        case GCS_MSG_STATE_MSG:
            ret = core_handle_state_msg (conn, recv_msg, &recv_act);
            assert (ret >= 0); // hang on error in debug mode
            break;
        case GCS_MSG_JOIN:
        case GCS_MSG_SYNC:
        case GCS_MSG_FLOW:
            ret = core_msg_to_action (conn, recv_msg, &recv_act);
            break;
	default:
	    // this normaly should not happen, shall we bother with
	    // protection?
	    gu_warn ("Received unsupported message type: %d, length: %d, "
                     "sender: %d",
		     recv_msg->type, recv_msg->size, recv_msg->sender_idx);
	    // continue looping
        }
    } /* end of recv loop */

out:
    assert (ret);

    *action   = recv_act.buf;
    *act_type = recv_act.type;
    *act_id   = recv_act.id;

//    gu_debug ("Returning %d", ret);

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
    core_act_t* tmp;

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
    gcs_fifo_lite_close (core->fifo);
    while ((tmp = gcs_fifo_lite_get_head (core->fifo))) {
        // whatever is in tmp.action is allocated by application,
        // just forget it.
        gcs_fifo_lite_pop_head (core->fifo);
    }
    gcs_fifo_lite_destroy (core->fifo);
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
                memset (conn->send_buf, 0, hdr_size); // to pacify valgrind
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

static inline long
core_send_seqno (gcs_core_t* core, gcs_seqno_t seqno, gcs_msg_type_t msg_type)
{
    gcs_seqno_t seqno_le = gcs_seqno_le (seqno);
    ssize_t     ret      = core_msg_send_retry (core, &seqno_le,
                                                sizeof(seqno_le), msg_type);
    if (ret > 0) {
        assert(ret == sizeof(seqno));
        ret = 0;
    }
    return ret;
}

long
gcs_core_set_last_applied (gcs_core_t* core, gcs_seqno_t seqno)
{
    return core_send_seqno (core, seqno, GCS_MSG_LAST);
}

long
gcs_core_send_join (gcs_core_t* core, gcs_seqno_t seqno)
{
    return core_send_seqno (core, seqno, GCS_MSG_JOIN);
}

long
gcs_core_send_fc (gcs_core_t* core, void* fc, size_t fc_size)
{
    ssize_t ret;
    ret = core_msg_send_retry (core, fc, fc_size, GCS_MSG_FLOW);
    if (ret == fc_size) {
        ret = 0;
    }
    return ret;
}
