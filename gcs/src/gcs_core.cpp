/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 *
 *
 * Implementation of the generic communication layer.
 * See gcs_core.h
 */

#define GCS_COMP_MSG_ACCESS

#include "gcs_core.hpp"

#include "gcs_backend.hpp"
#include "gcs_comp_msg.hpp"
#include "gcs_code_msg.hpp"
#include "gcs_fifo_lite.hpp"
#include "gcs_group.hpp"
#include "gcs_gcache.hpp"

#include <gu_throw.hpp>
#include <gu_logger.hpp>
#include <gu_serialize.hpp>
#include <gu_debug_sync.hpp>

#include <string.h> // for mempcpy
#include <errno.h>

using namespace gcs::core;

bool
gcs_core_register (gu_config_t* conf)
{
    gcs_group_register(reinterpret_cast<gu::Config*>(conf));
    return (gcs_backend_register(conf));
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
    gcs_seqno_t     code_msg_buf;

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
    gu_uuid_t*   act_uuid;
    long*        error;
    gu_mutex_t*  mtx;
    gu_cond_t*   cond;
} causal_act_t;

gcs_core_t*
gcs_core_create (gu_config_t* const conf,
                 gcache_t*    const cache,
                 const char*  const node_name,
                 const char*  const inc_addr,
                 int          const repl_proto_ver,
                 int          const appl_proto_ver,
                 int          const gcs_proto_ver)
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
                    core->proto_ver = -1;
                    // ^^^ shall be bumped in gcs_group_act_conf()

                    gcs_group_init (&core->group,
                                    reinterpret_cast<gu::Config*>(conf), cache,
                                    node_name, inc_addr,
                                    gcs_proto_ver, repl_proto_ver,appl_proto_ver
                        );

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
gcs_core_init (gcs_core_t* core, const gu::GTID& position)
{
    if (core->state == CORE_CLOSED) {
        return gcs_group_init_history (&core->group, position);
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
            if (to_copy <= left) {
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
        GU_DBUG_SYNC_WAIT("gcs_core_after_frag_send");
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

    assert(recv_msg->buf || 0 == recv_msg->buf_len);

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

    assert(recv_msg->buf);

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
    bool  commonly_supported_version = true;

    assert (GCS_MSG_ACTION == msg->type);

    if ((CORE_PRIMARY == core->state) || my_msg){//should always handle own msgs

        if (gu_unlikely(gcs_act_proto_ver(msg->buf) !=
                        gcs_core_proto_ver(core))) {
            gu_info ("Message with protocol version %d != highest commonly "
                     "supported: %d.",
                     gcs_act_proto_ver(msg->buf), gcs_core_proto_ver(core));
            commonly_supported_version = false;
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
                                        commonly_supported_version);

        if (ret > 0) { /* complete action received */
            assert (act->act.buf_len == ret);
#ifndef GCS_FOR_GARB
            assert (NULL != act->act.buf);
#else
            assert (NULL == act->act.buf);
#endif
            assert(act->sender_idx == msg->sender_idx);

            if (gu_likely(!my_msg)) {
                /* foreign action, must be passed from gcs_group */
                assert (GCS_ACT_WRITESET != act->act.type || act->id > 0);
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
    assert(GCS_MSG_LAST == msg->type);
    assert(CodeMsg::serial_size() >= msg->size);
    assert(int(sizeof(uint64_t)) <= msg->size);

    if (gu_likely(gcs_group_is_primary(&core->group))) {

        gcs_seqno_t const commit_cut
            (gcs_group_handle_last_msg(&core->group, msg));

        if (0 != commit_cut) {
            /* commit cut changed */
            int   const buf_len(sizeof(uint64_t));
            void* const buf(malloc(buf_len));

            if (gu_likely(NULL != (buf))) {
                /* #701 - everything that goes into the action buffer
                 *        is expected to be serialized. */
                gu::serialize8(commit_cut, buf, buf_len, 0);
                assert(NULL == act->buf);
                act->buf     = buf;
                act->buf_len = buf_len;
                act->type    = GCS_ACT_COMMIT_CUT;
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
 * Helper for gcs_core_recv(). Handles GCS_MSG_LAST.
 *
 * @return action size, negative error code or 0 to continue.
 */
static int
core_handle_vote_msg (gcs_core_t*          core,
                      struct gcs_recv_msg* msg,
                      struct gcs_act*      act)
{
    assert (GCS_MSG_VOTE == msg->type);
    assert (CodeMsg::serial_size() <= msg->size);

    VoteResult const res(gcs_group_handle_vote_msg(&core->group, msg));

    if (res.seqno != GCS_SEQNO_ILL)
    {
        assert(res.seqno > 0);
        /* voting complete or vote request */
        int   const buf_len(2 * sizeof(uint64_t));
        void* const buf(malloc(buf_len));

        if (gu_likely(NULL != (buf))) {
            gu::serialize8(res.seqno, buf, buf_len, 0);
            gu::serialize8(res.res,   buf, buf_len, 8);
            assert(NULL == act->buf);
            act->buf     = buf;
            act->buf_len = buf_len;
            act->type    = GCS_ACT_VOTE;
            return act->buf_len;
        }
        else {
            gu_fatal ("Out of memory for GCS_ACT_VOTE");
            return -ENOMEM;
        }
    }

    return 0;
}

/*! Common things to do on detected inconsistency */
static int
core_handle_inconsistency(gcs_core_t* core, struct gcs_act* act)
{
    core->state  = CORE_NON_PRIMARY;
    act->buf     = NULL;
    act->buf_len = 0;
    act->type    = GCS_ACT_INCONSISTENCY;
    return -ENOTRECOVERABLE;
}

/*!
 * Helper for gcs_core_recv(). Handles GCS_MSG_COMPONENT.
 *
 * @return action size, negative error code or 0 to continue.
 */
static ssize_t
core_handle_comp_msg (gcs_core_t*          const core,
                      struct gcs_recv_msg* const msg,
                      struct gcs_act_rcvd* const rcvd)
{
    ssize_t ret(0);
    gcs_group_t*    const group(&core->group);
    struct gcs_act* const act(&rcvd->act);

    assert (GCS_MSG_COMPONENT == msg->type);

    if (msg->size < (ssize_t)sizeof(gcs_comp_msg_t)) {
        gu_error ("Malformed component message (size %zd < %zd). Ignoring",
                  msg->size, sizeof(gcs_comp_msg_t));
        return 0;
    }

    if (gu_mutex_lock (&core->send_lock)) abort();
    ret = gcs_group_handle_comp_msg (group, (const gcs_comp_msg_t*)msg->buf);

    switch (ret) {
    case GCS_GROUP_PRIMARY:
        /* New primary configuration. This happens if:
         * - this is first node in group OR
         * - some nodes disappeared no new nodes appeared
         * No need for state exchange, return new conf_act right away */
        assert (CORE_EXCHANGE != core->state);
        if (CORE_NON_PRIMARY == core->state) core->state = CORE_PRIMARY;

        ret = gcs_group_act_conf (group, rcvd, &core->proto_ver);
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
        break;
    case GCS_GROUP_NON_PRIMARY:
        /* Lost primary component */
        if (core->state < CORE_CLOSED) {
            if (gcs_group_my_idx(group) == -1) { // self-leave
                gcs_fifo_lite_close (core->fifo);
                core->state = CORE_CLOSED;
                ret = -gcs_comp_msg_error((const gcs_comp_msg_t*)msg->buf);
                if (ret < 0) {
                    assert(act->buf == NULL);
                    assert(act->buf_len == 0);
                    act->type = GCS_ACT_ERROR;
                    gu_debug("comp msg error in core %d", -ret);
                }
            }
            else {                               // regular non-prim
                core->state = CORE_NON_PRIMARY;
            }

            if (GCS_GROUP_NON_PRIMARY == ret) { // no error in comp msg
                ret = gcs_group_act_conf (group, rcvd, &core->proto_ver);
                if (ret < 0) {
                    gu_fatal ("Failed create NON-PRIM CONF action: %d (%s)",
                              ret, strerror (-ret));
                    assert (0);
                    ret = -ENOTRECOVERABLE;
                }
            }
        }
        else { // ignore in production?
            assert(0);
        }
        assert (ret == act->buf_len || ret < 0);
        break;
    case GCS_GROUP_INCONSISTENT:
        ret = core_handle_inconsistency(core, act);
        break;
    case GCS_GROUP_WAIT_STATE_MSG:
        gu_fatal ("Internal error: gcs_group_handle_comp() returned "
                  "WAIT_STATE_MSG. Can't continue.");
        ret = -ENOTRECOVERABLE;
        assert(0);
        // fall through
    default:
        gu_fatal ("Failed to handle component message: %d (%s)!",
                  ret, strerror (-ret));
        assert(0);
    }
    gu_mutex_unlock (&core->send_lock);

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
                        gu_error ("STATE EXCHANGE: failed for: " GU_UUID_FORMAT
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
                       struct gcs_act_rcvd* rcvd)
{
    ssize_t      ret(0);
    gcs_group_t* const group(&core->group);

    assert (GCS_MSG_STATE_MSG == msg->type);

    if (GCS_GROUP_WAIT_STATE_MSG == gcs_group_state (group))
    {
        if (gu_mutex_lock (&core->send_lock)) abort();
        ret = gcs_group_handle_state_msg (group, msg);

        switch (ret) {
        case GCS_GROUP_PRIMARY:
        case GCS_GROUP_NON_PRIMARY:
            // state exchange is over, create configuration action
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

            ret = gcs_group_act_conf (group, rcvd, &core->proto_ver);
            if (ret < 0) {
                gu_fatal ("Failed create CONF action: %d (%s)",
                          ret, strerror (-ret));
                assert (0);
                ret = -ENOTRECOVERABLE;
            }
            assert (ret == rcvd->act.buf_len);
            break;
        case GCS_GROUP_WAIT_STATE_MSG:
            // waiting for more state messages
            ret = 0;
            break;
        case GCS_GROUP_INCONSISTENT:
            ret = core_handle_inconsistency(core, &rcvd->act);
            break;
        default:
            assert (ret < 0);
            gu_error ("Failed to handle state message: %d (%s)",
                      ret, strerror (-ret));
        }
        gu_mutex_unlock (&core->send_lock);
    }

    return ret;
}

/* returns code in serialized form */
static gcs_seqno_t
core_msg_code (const struct gcs_recv_msg* const msg, int const proto_ver)
{
    if (gu_likely(proto_ver >= 1 &&
                  msg->size == gcs::core::CodeMsg::serial_size()))
    {
        const gcs::core::CodeMsg* const cm
            (static_cast<const gcs::core::CodeMsg*>(msg->buf));
        return gu::htog(cm->code());
    }
    else if (proto_ver == 0 && msg->size == sizeof(gcs_seqno_t))
    {
        return *(static_cast<const gcs_seqno_t*>(msg->buf));
        // no deserialization
    }
    else
    {
        log_warn << "Bogus code message size: " << msg->size;
        assert(0);
        return gu::htog(gcs_seqno_t(-EINVAL));
    }
}

/*!
 * Some service actions are for internal use and consist of a single message
 * (FLOW, JOIN, SYNC)
 * In this case we can simply use msg->buf as an action buffer, since we
 * can guarantee that we don't deallocate it. Action here is just a wrapper
 * to deliver message to the upper level.
 */
static int
core_msg_to_action (gcs_core_t*          core,
                    struct gcs_recv_msg* msg,
                    struct gcs_act_rcvd* rcvd)
{
    int          ret = 0;
    gcs_group_t* group = &core->group;
    struct gcs_act* const act(&rcvd->act);

    if (GCS_GROUP_PRIMARY == gcs_group_state (group)) {
        switch (msg->type) {
        case GCS_MSG_FLOW: // most frequent
            ret = 1;
            act->type    = GCS_ACT_FLOW;
            act->buf     = msg->buf;
            act->buf_len = msg->size;
            break;
        case GCS_MSG_JOIN:
            ret = gcs_group_handle_join_msg (group, msg);
            assert (gcs_group_my_idx(group) == msg->sender_idx || 0 >= ret);
            if (-ENOTRECOVERABLE == ret) {
                core->backend.close(&core->backend);
                // See #165.
                // There is nobody to pass this error to for graceful shutdown:
                // application thread is blocked waiting for SST.
                // Also note that original ret value is not preserved on return
                // so this must be done here.
                gu_abort();
            }
            else if (ret != 0)
            {
                core->code_msg_buf = core_msg_code(msg, core->proto_ver);
                act->type    = GCS_ACT_JOIN;
                act->buf     = &core->code_msg_buf;
                act->buf_len = sizeof(core->code_msg_buf);
            }
            break;
        case GCS_MSG_SYNC:
            ret = gcs_group_handle_sync_msg (group, msg);
            if (gu_likely(ret != 0))
            {
                core->code_msg_buf = core_msg_code(msg, core->proto_ver);
                act->type    = GCS_ACT_SYNC;
                act->buf     = &core->code_msg_buf;
                act->buf_len = sizeof(core->code_msg_buf);
            }
            break;
        default:
            gu_error ("Iternal error. Unexpected message type %s from %ld",
                      gcs_msg_type_string[msg->type], msg->sender_idx);
            assert (0);
            ret = -EPROTO;
        }

        if (ret != 0) {
            if      (ret > 0) rcvd->id = 0;
            else if (ret < 0) rcvd->id = ret;

            ret = act->buf_len;
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
    if (gu_unlikely(msg->size != sizeof(causal_act_t)))
    {
        gu_error("invalid causal act len %ld, expected %ld",
                 msg->size, sizeof(causal_act_t));
        return -EPROTO;
    }

    causal_act_t* act = (causal_act_t*)msg->buf;
    gu_mutex_lock(act->mtx);
    {
        switch (conn->group.state)
        {
        case GCS_GROUP_PRIMARY:
            *act->act_id = conn->group.act_id_;
            *act->act_uuid = conn->group.group_uuid;
            break;
        case GCS_GROUP_WAIT_STATE_UUID:
        case GCS_GROUP_WAIT_STATE_MSG:
            *act->error = -EAGAIN;
            break;
        default:
            *act->error = -EPERM;
        }

        gu_cond_signal(act->cond);
    }
    gu_mutex_unlock(act->mtx);

    return msg->size;
}

/*! Receives action */
ssize_t gcs_core_recv (gcs_core_t*          conn,
                       struct gcs_act_rcvd* recv_act,
                       long long            timeout)
{
    struct gcs_recv_msg* const recv_msg(&conn->recv_msg);
    ssize_t ret(0);

    static struct gcs_act_rcvd zero_act(
        gcs_act(NULL,
                0,
                GCS_ACT_ERROR),
        NULL,
        GCS_SEQNO_ILL,
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

        assert(recv_msg->buf);
        assert(recv_msg->buf_len >= recv_msg->size);

        switch (recv_msg->type) {
        case GCS_MSG_ACTION:
            ret = core_handle_act_msg(conn, recv_msg, recv_act);
            assert (ret == recv_act->act.buf_len || ret <= 0);
            break;
        case GCS_MSG_LAST:
            ret = core_handle_last_msg(conn, recv_msg, &recv_act->act);
            assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len);
            break;
        case GCS_MSG_COMPONENT:
            ret = core_handle_comp_msg (conn, recv_msg, recv_act);
            // assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len || ret < 0);
            break;
        case GCS_MSG_STATE_UUID:
            ret = core_handle_uuid_msg (conn, recv_msg);
            // assert (ret >= 0); // hang on error in debug mode
            ret = 0;              // continue waiting for state messages
            break;
        case GCS_MSG_STATE_MSG:
            ret = core_handle_state_msg (conn, recv_msg, recv_act);
            // assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len || ret < 0);
            break;
        case GCS_MSG_JOIN:
        case GCS_MSG_SYNC:
        case GCS_MSG_FLOW:
            ret = core_msg_to_action (conn, recv_msg, recv_act);
            assert (ret == recv_act->act.buf_len || ret <= 0);
            break;
        case GCS_MSG_VOTE:
            ret = core_handle_vote_msg(conn, recv_msg, &recv_act->act);
            assert (ret >= 0); // hang on error in debug mode
            assert (ret == recv_act->act.buf_len);
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
    assert (recv_act->id       <= 0                ||
            recv_act->act.type == GCS_ACT_WRITESET ||
            recv_act->act.type == GCS_ACT_CCHANGE  ||
            recv_act->act.type == GCS_ACT_STATE_REQ); // <- dirty hack
    assert (recv_act->sender_idx >= 0 ||
            recv_act->act.type   != GCS_ACT_WRITESET);

//    gu_debug ("Returning %d", ret);

    if (gu_unlikely(ret < 0)) {
        assert (recv_act->id < 0);
        assert (GCS_ACT_CCHANGE != recv_act->act.type);

        if (GCS_ACT_WRITESET == recv_act->act.type && recv_act->act.buf) {
            gcs_gcache_free (conn->cache, recv_act->act.buf);
            recv_act->act.buf = NULL;
        }

        if (-ENOTRECOVERABLE == ret) {
            conn->backend.close(&conn->backend);
            if (GCS_ACT_INCONSISTENCY != recv_act->act.type) {
                /* inconsistency event must be passed up */
                gu_abort();
            }
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

int
gcs_core_proto_ver (const gcs_core_t* conn)
{
    return conn->proto_ver;
}

int
gcs_core_set_pkt_size (gcs_core_t* core, int const pkt_size)
{
    if (core->state >= CORE_CLOSED) {
        gu_error ("Attempt to set packet size on a closed connection.");
        return -EBADFD;
    }

    int const hdr_size(gcs_act_proto_hdr_size(core->proto_ver));
    if (hdr_size < 0) return hdr_size;

    int const min_msg_size(hdr_size + 1);

    int msg_size(core->backend.msg_size(&core->backend, pkt_size));
    if (msg_size < min_msg_size) {
        gu_warn ("Requested packet size %d is too small, "
                 "using smallest possible: %d",
                 pkt_size, pkt_size + (min_msg_size - msg_size));
        msg_size = min_msg_size;
    }

    /* even if backend may not support limiting packet size force max message
     * size at this level */
    msg_size = std::min(std::max(min_msg_size, pkt_size), msg_size);

    gu_info ("Changing maximum packet size to %d, resulting msg size: %d",
             pkt_size, msg_size);

    int ret(msg_size - hdr_size); // message payload
    assert(ret > 0);

    if (core->send_buf_len == (size_t)msg_size) return ret;

    if (gu_mutex_lock (&core->send_lock)) abort();
    {
        if (core->state != CORE_DESTROYED) {
            void* new_send_buf(gu_realloc(core->send_buf, msg_size));
            if (new_send_buf) {
                core->send_buf     = new_send_buf;
                core->send_buf_len = msg_size;
                memset (core->send_buf, 0, hdr_size); // to pacify valgrind
                gu_debug ("Message payload (action fragment size): %d", ret);
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

static inline ssize_t
core_send_seqno (gcs_core_t* core, gcs_seqno_t seqno, gcs_msg_type_t msg_type)
{
    gcs_seqno_t const htogs = gcs_seqno_htog (seqno);
    ssize_t           ret   = core_msg_send_retry (core, &htogs,
                                                   sizeof(htogs),
                                                   msg_type);
    if (ret > 0) { assert(ret == sizeof(seqno)); }

    return ret;
}

static inline int
core_send_code (gcs_core_t* const core, const gu::GTID& gtid, int64_t code,
                gcs_msg_type_t const msg_type)
{
    if (gu_unlikely(core->proto_ver < 1))
    {
        return core_send_seqno (core, code < 0 ? code : gtid.seqno(), msg_type);
    }

    CodeMsg const msg(gtid, code);
    assert(msg.uuid() != GU_UUID_NIL);

    int ret(core_msg_send_retry (core, msg(), msg.serial_size(), msg_type));

    if (ret > 0) { assert(ret == msg.serial_size()); }

    return ret;
}

int
gcs_core_set_last_applied (gcs_core_t* const core, const gu::GTID& gtid)
{
    return core_send_code (core, gtid, 0, GCS_MSG_LAST);
}

int
gcs_core_send_join (gcs_core_t* const core, const gu::GTID& gtid, int code)
{
    return core_send_code (core, gtid, code, GCS_MSG_JOIN);
}

int
gcs_core_send_sync (gcs_core_t* const core, const gu::GTID& gtid)
{
    return core_send_code (core, gtid, 0, GCS_MSG_SYNC);
}

int
gcs_core_send_vote (gcs_core_t* const core, const gu::GTID& gtid, int64_t code,
                    const void* data, size_t const data_len)
{
#if 0 // simple code message
    return core_send_code (core, gtid, code, GCS_MSG_VOTE);
#else
    CodeMsg const cmsg(gtid, code);
    assert(cmsg.uuid() != GU_UUID_NIL);
    int const cmsg_size(cmsg.serial_size());

    char vmsg[1024] = { 0, }; // try to fit in one ethernet frame
    assert(cmsg_size < int(sizeof(vmsg)));

    ::memcpy(&vmsg[0], cmsg(), cmsg_size);

    int copy_size(int(sizeof(vmsg)) - cmsg_size - 1); // allow for trailing 0
    assert(copy_size >= 0);
    if (size_t(copy_size) > data_len) copy_size = data_len;

    ::memcpy(&vmsg[cmsg_size], data, copy_size);

    int const vmsg_size(cmsg_size + copy_size + 1);

    int ret(core_msg_send_retry(core, &vmsg[0], vmsg_size, GCS_MSG_VOTE));

    if (ret > 0) { assert(ret >= cmsg_size); }

    return ret;
#endif
}

ssize_t
gcs_core_send_fc (gcs_core_t* core, const void* const fc, size_t const fc_size)
{
    ssize_t ret;
    ret = core_msg_send_retry (core, fc, fc_size, GCS_MSG_FLOW);
    if (ret == (ssize_t)fc_size) {
        ret = 0;
    }
    return ret;
}

long
gcs_core_caused (gcs_core_t* core, gu::GTID& gtid)
{
    long         error = 0;
    gcs_seqno_t  act_id = GCS_SEQNO_ILL;
    gu_uuid_t    act_uuid = GU_UUID_NIL;
    gu_mutex_t   mtx;
    gu_cond_t    cond;
    causal_act_t act = {&act_id, &act_uuid, &error, &mtx, &cond};

    gu_mutex_init (&mtx, NULL);
    gu_cond_init  (&cond, NULL);
    gu_mutex_lock (&mtx);
    {
        long ret = core_msg_send_retry (core,
                                        &act,
                                        sizeof(act),
                                        GCS_MSG_CAUSAL);

        if (ret == sizeof(act))
        {
            gu_cond_wait (&cond, &mtx);
            if (error == 0)
            {
              gtid.set (act_uuid, act_id);
            }
        }
        else
        {
            assert (ret < 0);
            error = ret;
        }
    }
    gu_mutex_unlock  (&mtx);
    gu_mutex_destroy (&mtx);
    gu_cond_destroy  (&cond);

    return error;
}

int
gcs_core_param_set (gcs_core_t* core, const char* key, const char* value)
{
    if (core->backend.conn) {
        return
            gcs_group_param_set(core->group, key, value) &&
            core->backend.param_set(&core->backend, key, value);
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
        gcs_group_get_status(&core->group, status);
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
gcs_core_get_group (const gcs_core_t* core)
{
    return &core->group;
}

gcs_fifo_lite_t*
gcs_core_get_fifo (gcs_core_t* core)
{
    return core->fifo;
}

#endif /* GCS_CORE_TESTING */
