/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 *
 *
 * Implementation of the generic communication layer.
 * See gcs_core.h
 */

#include "gu_throw.hpp"

#define GCS_COMP_MSG_ACCESS

#include "gcs_core.hpp"

#include "gcs_backend.hpp"
#include "gcs_comp_msg.hpp"
#include "gcs_fifo_lite.hpp"
#include "gcs_group.hpp"
#include "gcs_gcache.hpp"

#include <string.h> // for mempcpy
#include <errno.h>

bool
gcs_core_register (gu_config_t* conf)
{
    return gcs_backend_register(conf);
}

const size_t CORE_FIFO_LEN = (1 << 10); // 1024 elements (no need to have more)
const size_t CORE_INIT_BUF_SIZE = (1 << 16); // 65K - IP packet size

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
    gu_config_t*    config;
    gcache_t*       cache;

    /* connection per se */
    long            prim_comp_no;
    core_state_t    state;

    /* protocol */
    int             proto_ver;

    /* send part */
    gu_mutex_t      send_lock; // serves 3 purposes:
                               // 1) serializes access to backend send() call
                               // 2) synchronizes with configuration changes
                               // 3) synchronizes with close() call

    void*           send_buf;
    size_t          send_buf_len;
    gcs_seqno_t     send_act_no;

    /* recv part */
    gcs_recv_msg_t  recv_msg;

    /* local action FIFO */
    gcs_fifo_lite_t* fifo;

    /* group context */
    gcs_group_t     group;

    /* backend part */
    size_t          msg_size;
    gcs_backend_t   backend;   // message IO context

#ifdef GCS_CORE_TESTING
    gu_lock_step_t  ls;        // to lock-step in unit tests
    gu_uuid_t state_uuid;
#endif
};

// this is to pass local action info from send to recv thread.
typedef struct core_act
{
    gcs_seqno_t sent_act_id;
    const void* action;
    size_t      action_size;
}
core_act_t;

typedef struct causal_act
{
    gcs_seqno_t* act_id;
    gu_mutex_t*  mtx;
    gu_cond_t*   cond;
} causal_act_t;

static int const GCS_PROTO_MAX = 0;

gcs_core_t*
gcs_core_create (gu_config_t* const conf,
                 gcache_t*    const cache,
                 const char*  const node_name,
                 const char*  const inc_addr,
                 int          const repl_proto_ver,
                 int          const appl_proto_ver)
{
    assert (conf);

    gcs_core_t* core = GU_CALLOC (1, gcs_core_t);

    if (NULL != core) {

        core->config = conf;
        core->cache  = cache;

        // Need to allocate something, otherwise Spread 3.17.3 freaks out.
        core->recv_msg.buf = gu_malloc(CORE_INIT_BUF_SIZE);
        if (core->recv_msg.buf) {

            core->recv_msg.buf_len = CORE_INIT_BUF_SIZE;

            core->send_buf = GU_CALLOC(CORE_INIT_BUF_SIZE, char);
            if (core->send_buf) {

                core->send_buf_len = CORE_INIT_BUF_SIZE;

                core->fifo = gcs_fifo_lite_create (CORE_FIFO_LEN,
                                                   sizeof (core_act_t));
                if (core->fifo) {
                    gu_mutex_init  (&core->send_lock, NULL);
                    core->proto_ver = -1; // shall be bumped in gcs_group_act_conf()
                    gcs_group_init (&core->group, cache, node_name, inc_addr,
                                    GCS_PROTO_MAX, repl_proto_ver,
                                    appl_proto_ver);
                    core->state = CORE_CLOSED;
                    core->send_act_no = 1; // 0 == no actions sent
#ifdef GCS_CORE_TESTING
                    gu_lock_step_init (&core->ls);
                    core->state_uuid = GU_UUID_NIL;
#endif
                    return core; // success
                }

                gu_free (core->send_buf);
            }

            gu_free (core->recv_msg.buf);
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
               const char* channel,
               const char* url,
               bool const  bstrap)
{
    long ret;

    if (core->state != CORE_CLOSED) {
        gu_debug ("gcs_core->state isn't CLOSED: %d", core->state);
        return -EBADFD;
    }

    if (core->backend.conn) {
        assert (core->backend.destroy);
        core->backend.destroy (&core->backend);
        memset (&core->backend, 0, sizeof(core->backend));
    }

    gu_debug ("Initializing backend IO layer");
    if (!(ret = gcs_backend_init (&core->backend, url, core->config))){

        assert (NULL != core->backend.conn);

        if (!(ret = core->backend.open (&core->backend, channel, bstrap))) {
            gcs_fifo_lite_open (core->fifo);
            core->state = CORE_NON_PRIMARY;
        }
        else {
            gu_error ("Failed to open backend connection: %d (%s)",
                      ret, strerror(-ret));
            core->backend.destroy (&core->backend);
        }

    }
    else {
        gu_error ("Failed to initialize backend using '%s': %d (%s)",
                  url, ret, strerror(-ret));
    }

    return ret;
}

/* Translates different core states into standard errors */
static inline ssize_t
core_error (core_state_t state)
{
    switch (state) {
    case CORE_EXCHANGE:    return -EAGAIN;
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
               const void*    msg,
               size_t         msg_len,
               gcs_msg_type_t msg_type)
{
    ssize_t ret;

    if (gu_unlikely(0 != gu_mutex_lock (&core->send_lock))) abort();
    {
        if (gu_likely((CORE_PRIMARY  == core->state) ||
                      (CORE_EXCHANGE == core->state && GCS_MSG_STATE_MSG ==
                       msg_type))) {

            ret = core->backend.send (&core->backend, msg, msg_len, msg_type);

            if (ret > 0 && ret != (ssize_t)msg_len &&
                GCS_MSG_ACTION != msg_type) {
                // could not send message in one piece
                gu_error ("Failed to send complete message of %s type: "
                          "sent %zd out of %zu bytes.",
                          gcs_msg_type_string[msg_type], ret, msg_len);
                ret = -EMSGSIZE;
            }
        }
        else {
            ret = core_error (core->state);

            if (ret >= 0) {
                gu_fatal ("GCS internal state inconsistency: "
                          "expected error condition.");
                abort(); // ret = -ENOTRECOVERABLE;
            }
        }
    }
    gu_mutex_unlock (&core->send_lock);
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
        gu_debug ("Backend requested wait");
        usleep (10000);
    }
//    gu_debug ("returning: %d (%s)", ret, strerror(-ret));
    return ret;
}

ssize_t
gcs_core_send (gcs_core_t*          const conn,
               const struct gu_buf* const action,
               size_t                     act_size,
               gcs_act_type_t       const act_type)
{
    ssize_t        ret  = 0;
    ssize_t        sent = 0;
    gcs_act_frag_t frg;
    ssize_t        send_size;
    const unsigned char proto_ver = conn->proto_ver;
    const ssize_t  hdr_size       = gcs_act_proto_hdr_size (proto_ver);

    core_act_t*    local_act;

    assert (action != NULL);
    assert (act_size > 0);

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
        return ret;

    if ((local_act = (core_act_t*)gcs_fifo_lite_get_tail (conn->fifo))) {
        *local_act = (core_act_t){ conn->send_act_no, action, act_size };
        gcs_fifo_lite_push_tail (conn->fifo);
    }
    else {
        ret = core_error (conn->state);
        gu_error ("Failed to access core FIFO: %d (%s)", ret, strerror (-ret));
        return ret;
    }

    int            idx  = 0;
    const uint8_t* ptr  = (const uint8_t*)action[idx].ptr;
    size_t         left = action[idx].size;

    do {
        const size_t chunk_size =
            act_size < frg.frag_len ? act_size : frg.frag_len;

        /* Here is the only time we have to cast frg.frag */
        char* dst = (char*)frg.frag;
        size_t to_copy = chunk_size;

        while (to_copy > 0) {        // gather action bufs into one
            if (to_copy < left) {
                memcpy (dst, ptr, to_copy);
                ptr     += to_copy;
                left    -= to_copy;
                to_copy = 0;
            }
            else {
                memcpy (dst, ptr, left);
                dst     += left;
                to_copy -= left;
                idx++;
                ptr  = (const uint8_t*)action[idx].ptr;
                left = action[idx].size;
            }
        }

        send_size = hdr_size + chunk_size;

#ifdef GCS_CORE_TESTING
        gu_lock_step_wait (&conn->ls); // pause after every fragment
        gu_info ("Sent %p of size %zu. Total sent: %zu, left: %zu",
                 (char*)conn->send_buf + hdr_size, chunk_size, sent, act_size);
#endif
        ret = core_msg_send_retry (conn, conn->send_buf, send_size,
                                   GCS_MSG_ACTION);
#ifdef GCS_CORE_TESTING
//        gu_lock_step_wait (&conn->ls); // pause after every fragment
//        gu_info ("Sent %p of size %zu, ret: %zd. Total sent: %zu, left: %zu",
//                 conn->send_buf + hdr_size, chunk_size, ret, sent, act_size);
#endif

        if (gu_likely(ret > hdr_size)) {

            assert (ret <= send_size);

            ret      -= hdr_size;
            sent     += ret;
            act_size -= ret;

            if (gu_unlikely((size_t)ret < chunk_size)) {
                /* Could not send all that was copied: */

                /* 1. adjust frag_len, don't copy more than we could send */
                frg.frag_len = ret;

                /* 2. move ptr back to point at the first unsent byte */
                size_t move_back = chunk_size - ret;
                size_t ptrdiff   = ptr - (uint8_t*)action[idx].ptr;
                do {
                    if (move_back <= ptrdiff) {
                        ptr -= move_back;
                        left = action[idx].size - ptrdiff + move_back;
                        break;
                    }
                    else {
                        assert (idx > 0);
                        move_back -= ptrdiff;
                        idx--;
                        ptrdiff = action[idx].size;
                        ptr = (uint8_t*)action[idx].ptr + ptrdiff;
                    }
                } while (true);
            }
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
             * 1. Action will never be received completely by this node. Hence
             *    action must be removed from fifo on behalf of sending thr.: */
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
core_msg_recv (gcs_backend_t* backend, gcs_recv_msg_t* recv_msg,
               long long timeout)
{
    long ret;

    ret = backend->recv (backend, recv_msg, timeout);

    while (gu_unlikely(ret > recv_msg->buf_len)) {
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

            ret = backend->recv (backend, recv_msg, timeout);

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

    if (gu_unlikely(ret < 0)) {
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
core_handle_act_msg (gcs_core_t*          core,
                     struct gcs_recv_msg* msg,
                     struct gcs_act_rcvd* act)
{
    ssize_t        ret = -1;
    gcs_group_t*   group = &core->group;
    gcs_act_frag_t frg;
    bool  my_msg = (gcs_group_my_idx(group) == msg->sender_idx);
    bool  not_commonly_supported_version = false;

    assert (GCS_MSG_ACTION == msg->type);

    if ((CORE_PRIMARY == core->state) || my_msg){//should always handle own msgs

        if (gu_unlikely(gcs_act_proto_ver(msg->buf) !=
                        gcs_core_group_protocol_version(core))) {
            gu_info ("Message with protocol version %d != highest commonly supported: %d. ",
                     gcs_act_proto_ver(msg->buf),
                     gcs_core_group_protocol_version(core));
            not_commonly_supported_version = true;
            if (!my_msg) {
                gu_info ("Discard message from member %d because of "
                         "not commonly supported version.", msg->sender_idx);
                return 0;
            } else {
                gu_info ("Resend message because of "
                         "not commonly supported version.");
            }
        }

        ret = gcs_act_proto_read (&frg, msg->buf, msg->size);

        if (gu_unlikely(ret)) {
            gu_fatal ("Error parsing action fragment header: %zd (%s).",
                      ret, strerror (-ret));
            assert (0);
            return -ENOTRECOVERABLE;
        }

        ret = gcs_group_handle_act_msg (group, &frg, msg, act,
                                        not_commonly_supported_version);

        if (ret > 0) { /* complete action received */
            assert (ret  == act->act.buf_len);
#ifndef GCS_FOR_GARB
            assert (NULL != act->act.buf);
#else
            assert (NULL == act->act.buf);
//            act->act.buf_len = 0;
#endif
            act->sender_idx = msg->sender_idx;

            if (gu_likely(!my_msg)) {
                /* foreign action, must be passed from gcs_group */
                assert (GCS_ACT_TORDERED != act->act.type || act->id > 0);
            }
            else {
                /* local action, get from FIFO, should be there already */
                core_act_t* local_act;
                gcs_seqno_t sent_act_id;

                if ((local_act = (core_act_t*)gcs_fifo_lite_get_head (
                         core->fifo))){
                    act->local       = (const struct gu_buf*)local_act->action;
                    act->act.buf_len = local_act->action_size;
                    sent_act_id      = local_act->sent_act_id;
                    gcs_fifo_lite_pop_head (core->fifo);

                    assert (NULL != act->local);

                    /* NOTE! local_act cannot be used after this point */
                    /* sanity check */
                    if (gu_unlikely(sent_act_id != frg.act_id)) {
                        gu_fatal ("FIFO violation: expected sent_act_id %lld "
                                  "found %lld", sent_act_id, frg.act_id);
                        ret = -ENOTRECOVERABLE;
                    }
                    if (gu_unlikely(act->act.buf_len != ret)) {
                        gu_fatal ("Send/recv action size mismatch: %zd/%zd",
                                  act->act.buf_len, ret);
                        ret = -ENOTRECOVERABLE;
                    }
                }
                else {
                    gu_fatal ("FIFO violation: queue empty when local action "
                              "received");
                    ret = -ENOTRECOVERABLE;
                }

                assert (act->id < 0 || CORE_PRIMARY == core->state);

                if (gu_unlikely(CORE_PRIMARY != core->state)) {
                    // there can be a tiny race with gcs_core_close(),
                    // so CORE_CLOSED allows TO delivery.
                    assert (act->id < 0 /*#275|| CORE_CLOSED == core->state*/);
                    if (act->id < 0) act->id = core_error (core->state);
                }
            }

            if (gu_unlikely(GCS_ACT_STATE_REQ == act->act.type && ret > 0 &&
                            // note: #gh74.
                            // if lingering STR sneaks in when core->state != CORE_PRIMARY
                            // act->id != GCS_SEQNO_ILL (most likely act->id == -EAGAIN)
                            core->state == CORE_PRIMARY)) {
#ifdef GCS_FOR_GARB
            /* ignoring state requests from other nodes (not allocated) */
            if (my_msg) {
                if (act->act.buf_len != act->local[0].size) {
                    gu_fatal ("Protocol violation: state request is fragmented."
                              " Aborting.");
                    abort();
                }
                act->act.buf = act->local[0].ptr;
#endif
                ret = gcs_group_handle_state_request (group, act);
                assert (ret <= 0 || ret == act->act.buf_len);
#ifdef GCS_FOR_GARB
                if (ret < 0) gu_fatal ("Handling state request failed: %d",ret);
                act->act.buf = NULL;
            }
            else {
                act->act.buf_len = 0;
                act->act.type    = GCS_ACT_ERROR;
                act->id          = GCS_SEQNO_ILL;
                act->sender_idx  = -1;
                ret = 0;
            }
#endif
            }
//          gu_debug ("Received action: seqno: %lld, sender: %d, size: %d, "
//                    "act: %p", act->id, msg->sender_idx, ret, act->buf);
//          gu_debug ("%s", (char*) act->buf);
        }
        else if (gu_unlikely(ret < 0)){
            gu_fatal ("Failed to handle action fragment: %zd (%s)",
                      ret, strerror(-ret));
            assert (0);
            return -ENOTRECOVERABLE;
        }
    }
    else {
        /* Non-primary conf, foreign message - ignore */
        gu_warn ("Action message in non-primary configuration from "
                 "member %d", msg->sender_idx);
        ret = 0;
    }

#ifndef NDEBUG
    if (ret <= 0) {
        assert (GCS_SEQNO_ILL == act->id);
        assert (GCS_ACT_ERROR == act->act.type);
    }
#endif

    return ret;
}

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_LAST.
 *
 * @return action size, negative error code or 0 to continue.
 */
static ssize_t
core_handle_last_msg (gcs_core_t*          core,
                      struct gcs_recv_msg* msg,
                      struct gcs_act*      act)
{
    assert (GCS_MSG_LAST == msg->type);

    if (gcs_group_is_primary(&core->group)) {
        gcs_seqno_t commit_cut =
            gcs_group_handle_last_msg (&core->group, msg);
        if (commit_cut) {
            /* commit cut changed */
            if ((act->buf = malloc (sizeof (commit_cut)))) {
                act->type                 = GCS_ACT_COMMIT_CUT;
                /* #701 - everything that goes into the action buffer
                 *        is expected to be serialized. */
                *((gcs_seqno_t*)act->buf) = gcs_seqno_htog(commit_cut);
                act->buf_len              = sizeof(commit_cut);
                return act->buf_len;
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
core_handle_comp_msg (gcs_core_t*          core,
                      struct gcs_recv_msg* msg,
                      struct gcs_act*      act)
{
    ssize_t      ret = 0;
    gcs_group_t* group = &core->group;

    assert (GCS_MSG_COMPONENT == msg->type);

    if (msg->size < (ssize_t)sizeof(gcs_comp_msg_t)) {
        gu_error ("Malformed component message. Ignoring");
        return 0;
    }

    ret = gcs_group_handle_comp_msg (group, (const gcs_comp_msg_t*)msg->buf);

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
        }
        gu_mutex_unlock (&core->send_lock);

        ret = gcs_group_act_conf (group, act, &core->proto_ver);
        if (ret < 0) {
            gu_fatal ("Failed create PRIM CONF action: %d (%s)",
                      ret, strerror (-ret));
            assert (0);
            ret = -ENOTRECOVERABLE;
        }
        assert (ret == act->buf_len);
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
#ifdef GCS_CORE_TESTING
                    if (gu_uuid_compare(&core->state_uuid, &GU_UUID_NIL)) {
                        uuid = core->state_uuid;
                    }
#endif
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
            }
            ret = 0; // no action to return, continue
        }
        gu_mutex_unlock (&core->send_lock);
        break;
    case GCS_GROUP_NON_PRIMARY:
        /* Lost primary component */
        if (gu_mutex_lock (&core->send_lock)) abort();
        {
            if (core->state < CORE_CLOSED) {
                ret = gcs_group_act_conf (group, act, &core->proto_ver);
                if (ret < 0) {
                    gu_fatal ("Failed create NON-PRIM CONF action: %d (%s)",
                              ret, strerror (-ret));
                    assert (0);
                    ret = -ENOTRECOVERABLE;
                }

                if (gcs_group_my_idx(group) == -1) { // self-leave
                    core->state = CORE_CLOSED;
                }
                else {                               // regular non-prim
                    core->state = CORE_NON_PRIMARY;
                }
            }
            else { // ignore in production?
                assert(0);
            }
        }
        gu_mutex_unlock (&core->send_lock);
        assert (ret == act->buf_len);
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
                gcs_state_msg_t* state = gcs_group_get_state (group);

                if (state) {
                    size_t           state_len = gcs_state_msg_len (state);
                    uint8_t          state_buf[state_len];
                    const gu_uuid_t* state_uuid = gcs_state_msg_uuid (state);

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
                    gcs_state_msg_destroy (state);
                }
                else {
                    gu_fatal ("Failed to allocate state object.");
                    ret = -ENOTRECOVERABLE;
                }
            }
            break;
        case GCS_GROUP_WAIT_STATE_UUID:
            // In case of stray state uuid message
            break;
        default:
            assert(ret < 0);
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
core_handle_state_msg (gcs_core_t*          core,
                       struct gcs_recv_msg* msg,
                       struct gcs_act*      act)
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

            ret = gcs_group_act_conf (group, act, &core->proto_ver);
            if (ret < 0) {
                gu_fatal ("Failed create CONF action: %d (%s)",
                          ret, strerror (-ret));
                assert (0);
                ret = -ENOTRECOVERABLE;
            }
            assert (ret == act->buf_len);
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
core_msg_to_action (gcs_core_t*          core,
                    struct gcs_recv_msg* msg,
                    struct gcs_act*      act)
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
            assert (gcs_group_my_idx(group) == msg->sender_idx || 0 >= ret);
            if (-ENOTRECOVERABLE == ret) {
                core->backend.close(&core->backend);
                // See #165.
                // There is nobody to pass this error to for graceful shutdown:
                // application thread is blocked waiting for SST.
                gu_abort();
            }
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
            act->type    = act_type;
            act->buf     = msg->buf;
            act->buf_len = msg->size;
            ret          = msg->size;
        }
    }
    else {
        gu_warn ("%s message from member %ld in non-primary configuration. "
                 "Ignored.", gcs_msg_type_string[msg->type], msg->sender_idx);
    }

    return ret;
}

static long core_msg_causal(gcs_core_t* conn,
                            struct gcs_recv_msg* msg)
{
    causal_act_t* act;
    if (gu_unlikely(msg->size != sizeof(*act)))
    {
        gu_error("invalid causal act len %ld, expected %ld",
                 msg->size, sizeof(*act));
        return -EPROTO;
    }

    gcs_seqno_t const causal_seqno =
        GCS_GROUP_PRIMARY == conn->group.state ?
        conn->group.act_id : GCS_SEQNO_ILL;

    act = (causal_act_t*)msg->buf;
    gu_mutex_lock(act->mtx);
    *act->act_id = causal_seqno;
    gu_cond_signal(act->cond);
    gu_mutex_unlock(act->mtx);
    return msg->size;
}

/*! Receives action */
ssize_t gcs_core_recv (gcs_core_t*          conn,
                       struct gcs_act_rcvd* recv_act,
                       long long            timeout)
{
//    struct gcs_act_rcvd  recv_act;
    struct gcs_recv_msg* recv_msg = &conn->recv_msg;
    ssize_t              ret      = 0;

    static struct gcs_act_rcvd zero_act(
        gcs_act(NULL,
                0,
                GCS_ACT_ERROR),
        NULL,
        -1,   // GCS_SEQNO_ILL
        -1);

    *recv_act = zero_act;

    /* receive messages from group and demultiplex them
     * until finally some complete action is ready */
    do
    {
        assert (recv_act->act.buf     == NULL);
        assert (recv_act->act.buf_len == 0);
        assert (recv_act->act.type    == GCS_ACT_ERROR);
        assert (recv_act->id          == GCS_SEQNO_ILL);
        assert (recv_act->sender_idx  == -1);

        ret = core_msg_recv (&conn->backend, recv_msg, timeout);
        if (gu_unlikely (ret <= 0)) {
           goto out; /* backend error while receiving message */
        }

        switch (recv_msg->type) {
        case GCS_MSG_ACTION:
            ret = core_handle_act_msg(conn, recv_msg, recv_act);
            assert (ret == recv_act->act.buf_len || ret <= 0);
            assert (recv_act->sender_idx >= 0    || ret == 0);
            break;
        case GCS_MSG_LAST:
            ret = core_handle_last_msg(conn, recv_msg, &recv_act->act);
            assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len);
            break;
        case GCS_MSG_COMPONENT:
            ret = core_handle_comp_msg (conn, recv_msg, &recv_act->act);
            // assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len || ret <= 0);
            break;
        case GCS_MSG_STATE_UUID:
            ret = core_handle_uuid_msg (conn, recv_msg);
            // assert (ret >= 0); // hang on error in debug mode
            ret = 0;           // continue waiting for state messages
            break;
        case GCS_MSG_STATE_MSG:
            ret = core_handle_state_msg (conn, recv_msg, &recv_act->act);
            assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len);
            break;
        case GCS_MSG_JOIN:
        case GCS_MSG_SYNC:
        case GCS_MSG_FLOW:
            ret = core_msg_to_action (conn, recv_msg, &recv_act->act);
            assert (ret == recv_act->act.buf_len || ret <= 0);
            break;
        case GCS_MSG_CAUSAL:
            ret = core_msg_causal(conn, recv_msg);
            assert(recv_msg->sender_idx == gcs_group_my_idx(&conn->group));
            assert(ret == recv_msg->size || ret <= 0);
            ret = 0; // continue waiting for messages
            break;
        default:
            // this normaly should not happen, shall we bother with
            // protection?
            gu_warn ("Received unsupported message type: %d, length: %d, "
                     "sender: %d",
            recv_msg->type, recv_msg->size, recv_msg->sender_idx);
            // continue looping
        }
    } while (0 == ret); /* end of recv loop */

out:

    assert (ret || GCS_ACT_ERROR == recv_act->act.type);
    assert (ret == recv_act->act.buf_len || ret < 0);
    assert (recv_act->id       <= GCS_SEQNO_ILL ||
            recv_act->act.type == GCS_ACT_TORDERED ||
            recv_act->act.type == GCS_ACT_STATE_REQ); // <- dirty hack
    assert (recv_act->sender_idx >= 0 ||
            recv_act->act.type   != GCS_ACT_TORDERED);

//    gu_debug ("Returning %d", ret);

    if (ret < 0) {
        assert (recv_act->id < 0);

        if (GCS_ACT_TORDERED == recv_act->act.type && recv_act->act.buf) {
            gcs_gcache_free (conn->cache, recv_act->act.buf);
            recv_act->act.buf = NULL;
        }

        if (-ENOTRECOVERABLE == ret) {
            conn->backend.close(&conn->backend);
            gu_abort();
        }
    }

    return ret;
}

long gcs_core_close (gcs_core_t* core)
{
    long ret;

    if (!core) return -EBADFD;
    if (gu_mutex_lock (&core->send_lock)) return -EBADFD;

    if (core->state >= CORE_CLOSED) {
        ret = -EBADFD;
    }
    else {
        ret = core->backend.close (&core->backend);
        gcs_fifo_lite_close (core->fifo);
    }
    gu_mutex_unlock (&core->send_lock);

    return ret;
}

long gcs_core_destroy (gcs_core_t* core)
{
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

        if (core->backend.conn) {
            gu_debug ("Calling backend.destroy()");
            core->backend.destroy (&core->backend);
        }

        core->state = CORE_DESTROYED;
    }
    gu_mutex_unlock (&core->send_lock);
    /* at this point all send attempts are isolated */

    /* after that we must be able to destroy mutexes */
    while (gu_mutex_destroy (&core->send_lock));
    /* now noone will interfere */
    while ((tmp = (core_act_t*)gcs_fifo_lite_get_head (core->fifo))) {
        // whatever is in tmp.action is allocated by app., just forget it.
        gcs_fifo_lite_pop_head (core->fifo);
    }
    gcs_fifo_lite_destroy (core->fifo);
    gcs_group_free (&core->group);

    /* free buffers */
    gu_free (core->recv_msg.buf);
    gu_free (core->send_buf);

#ifdef GCS_CORE_TESTING
    gu_lock_step_destroy (&core->ls);
#endif

    gu_free (core);

    return 0;
}

gcs_proto_t
gcs_core_group_protocol_version (const gcs_core_t* conn)
{
    return conn->proto_ver;
}

long
gcs_core_set_pkt_size (gcs_core_t* core, long pkt_size)
{
    long     hdr_size, msg_size;
    uint8_t* new_send_buf = NULL;
    long     ret = 0;

    if (core->state >= CORE_CLOSED) {
        gu_error ("Attempt to set packet size on a closed connection.");
        return -EBADFD;
    }

    hdr_size = gcs_act_proto_hdr_size (core->proto_ver);
    if (hdr_size < 0) return hdr_size;

    msg_size = core->backend.msg_size (&core->backend, pkt_size);
    if (msg_size <= hdr_size) {
        gu_warn ("Requested packet size %d is too small, "
                 "using smallest possible: %d",
                 pkt_size, pkt_size + (hdr_size - msg_size + 1));
        msg_size = hdr_size + 1;
    }

    gu_info ("Changing maximum packet size to %ld, resulting msg size: %ld",
              pkt_size, msg_size);

    ret = msg_size - hdr_size; // message payload

    if (core->send_buf_len == (size_t)msg_size) return ret;

    if (gu_mutex_lock (&core->send_lock)) abort();
    {
        if (core->state != CORE_DESTROYED) {
            new_send_buf = (uint8_t*)gu_realloc (core->send_buf, msg_size);
            if (new_send_buf) {
                core->send_buf     = new_send_buf;
                core->send_buf_len = msg_size;
                memset (core->send_buf, 0, hdr_size); // to pacify valgrind
                gu_debug ("Message payload (action fragment size): %ld", ret);
            }
            else {
                ret = -ENOMEM;
            }
        }
        else {
            ret =  -EBADFD;
        }
    }
    gu_mutex_unlock (&core->send_lock);

    return ret;
}

static inline long
core_send_seqno (gcs_core_t* core, gcs_seqno_t seqno, gcs_msg_type_t msg_type)
{
    gcs_seqno_t const htogs = gcs_seqno_htog (seqno);
    ssize_t           ret   = core_msg_send_retry (core, &htogs,
                                                   sizeof(htogs),
                                                   msg_type);
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
gcs_core_send_sync (gcs_core_t* core, gcs_seqno_t seqno)
{
    return core_send_seqno (core, seqno, GCS_MSG_SYNC);
}

long
gcs_core_send_fc (gcs_core_t* core, const void* fc, size_t fc_size)
{
    ssize_t ret;
    ret = core_msg_send_retry (core, fc, fc_size, GCS_MSG_FLOW);
    if (ret == (ssize_t)fc_size) {
        ret = 0;
    }
    return ret;
}

gcs_seqno_t
gcs_core_caused(gcs_core_t* core)
{
    long         ret;
    gcs_seqno_t  act_id = GCS_SEQNO_ILL;
    gu_mutex_t   mtx;
    gu_cond_t    cond;
    causal_act_t act = {&act_id, &mtx, &cond};

    gu_mutex_init (&mtx, NULL);
    gu_cond_init  (&cond, NULL);
    gu_mutex_lock (&mtx);
    {
        ret = core_msg_send_retry (core, &act, sizeof(act), GCS_MSG_CAUSAL);

        if (ret == sizeof(act))
        {
            gu_cond_wait (&cond, &mtx);
        }
        else
        {
            assert (ret < 0);
            act_id = ret;
        }
    }
    gu_mutex_unlock  (&mtx);
    gu_mutex_destroy (&mtx);
    gu_cond_destroy  (&cond);

    return act_id;
}

long
gcs_core_param_set (gcs_core_t* core, const char* key, const char* value)
{
    if (core->backend.conn) {
        return core->backend.param_set (&core->backend, key, value);
    }
    else {
        return 1;
    }
}

const char*
gcs_core_param_get (gcs_core_t* core, const char* key)
{
    if (core->backend.conn) {
        return core->backend.param_get (&core->backend, key);
    }
    else {
        return NULL;
    }
}

void gcs_core_get_status(gcs_core_t* core, gu::Status& status)
{
    if (gu_mutex_lock(&core->send_lock))
        gu_throw_fatal << "could not lock mutex";
    if (core->state < CORE_CLOSED)
    {
        core->backend.status_get(&core->backend, status);
    }
    gu_mutex_unlock(&core->send_lock);
}

#ifdef GCS_CORE_TESTING

gcs_backend_t*
gcs_core_get_backend (gcs_core_t* core)
{
    return &core->backend;
}

void
gcs_core_send_lock_step (gcs_core_t* core, bool enable)
{
    gu_lock_step_enable (&core->ls, enable);
}

long
gcs_core_send_step (gcs_core_t* core, long timeout_ms)
{
    return gu_lock_step_cont (&core->ls, timeout_ms);
}

void
gcs_core_set_state_uuid (gcs_core_t* core, const gu_uuid_t* uuid)
{
    core->state_uuid = *uuid;
}

const gcs_group_t*
gcs_core_get_group (gcs_core_t* core)
{
    return &core->group;
}
#endif /* GCS_CORE_TESTING */
