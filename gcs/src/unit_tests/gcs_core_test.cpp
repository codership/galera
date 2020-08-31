/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*
 * @file
 *
 * Defines unit tests for gcs_core (and as a result tests gcs_group and
 * a dummy backend which gcs_core depends on)
 *
 * Most of the checks require independent sending and receiving threads.
 * Approach 1 is to start separate threads for both sending and receiving
 * and use the current thread of execution to sychronize between them:
 *
 * CORE_RECV_START(act_r)
 * CORE_SEND_START(act_s)
 * while (gcs_core_send_step(Core)) {  // step through action fragments
 *     (do something)
 * };
 * CORE_SEND_END(act_s, ret)          // check return code
 * CORE_RECV_END(act_r, size, type)   // makes checks against size and type
 *
 * A simplified approach 2 is:
 *
 * CORE_SEND_START(act_s)
 * while (gcs_core_send_step(Core)) {  // step through action fragments
 *     (do something)
 * };
 * CORE_SEND_END(act_s, ret)          // check return code
 * CORE_RECV_ACT(act_r, size, type)   // makes checks agains size and type
 *
 * In the first approach group messages will be received concurrently.
 * In the second approach messages will wait in queue and be fetched afterwards
 *
 */

#define GCS_STATE_MSG_ACCESS
#include "../gcs_core.hpp"
#include "../gcs_dummy.hpp"
#include "../gcs_seqno.hpp"
#include "../gcs_state_msg.hpp"

#include <galerautils.h>
#include "gu_config.hpp"

#include <errno.h>
#include <stdlib.h>
#include <check.h>

#include "gcs_core_test.hpp"

extern ssize_t gcs_tests_get_allocated();

static const long UNKNOWN_SIZE = 1234567890; // some unrealistic number

static gcs_core_t*    Core    = NULL;
static gcs_backend_t* Backend = NULL;
static gcs_seqno_t    Seqno   = 0;

typedef struct action {
    const struct gu_buf* in;
    void*                out;
    const void*          local;
    ssize_t              size;
    gcs_act_type_t       type;
    gcs_seqno_t          seqno;
    gu_thread_t          thread;

    action() { }
    action(const struct gu_buf* a_in,
           void*                a_out,
           const void*          a_local,
           ssize_t              a_size,
           gcs_act_type_t       a_type,
           gcs_seqno_t          a_seqno,
           gu_thread_t          a_thread)
        :
        in     (a_in),
        out    (a_out),
        local  (a_local),
        size   (a_size),
        type   (a_type),
        seqno  (a_seqno),
        thread (a_thread)
    { }
} action_t;

//static struct action_t RecvAct;
static const ssize_t FRAG_SIZE = 4; // desirable action fragment size

// 1-fragment action
static const char act1_str[] = "101";
static const struct gu_buf act1[1] = {
    { act1_str, sizeof(act1_str) }
};

// 2-fragment action, with buffers aligned with FRAG_SIZE
static const char act2_str[] = "202122";
static const struct gu_buf act2[2] = {
    { "2021", 4 },
    { "22",   3 } /* 4 + 3 = 7 = sizeof(act2_str) */
};

// 3-fragment action, with unaligned buffers
static const char act3_str[] = "3031323334";
static const struct gu_buf act3[] = {
    { "303", 3 },
    { "13",  2 },
    { "23",  2 },
    { "334", 4 } /* 3 + 2 + 2 + 4 = 11 = sizeof(act3_str) */
};


// action receive thread, returns after first action received, stores action
// in the passed action_t object, uses global Core to receive
static void*
core_recv_thread (void* arg)
{
    action_t* act = (action_t*)arg;

    // @todo: refactor according to new gcs_act types
    struct gcs_act_rcvd recv_act;

    act->size  = gcs_core_recv (Core, &recv_act, GU_TIME_ETERNITY);
    act->out   = (void*)recv_act.act.buf;
    act->local = recv_act.local;
    act->type  = recv_act.act.type;
    act->seqno = recv_act.id;

    return (NULL);
}

// this macro logs errors from within a function
#define FAIL_IF(expr, format, ...)                            \
    if (expr) {                                               \
        gu_fatal ("FAIL: " format, __VA_ARGS__, NULL);        \
        ck_assert_msg(false, format, __VA_ARGS__);      \
        return true;                                          \
    }

/*
 * Huge macros which follow below cannot be functions for the purpose
 * of correct line reporting.
 */

// Start a thread to receive an action
// args: action_t object
static inline bool CORE_RECV_START(action_t* act)
{
    return (0 != gu_thread_create (&act->thread, NULL,
                                   core_recv_thread, act));
}

static bool COMMON_RECV_CHECKS(action_t*      act,
                               const char*    buf,
                               ssize_t        size,
                               gcs_act_type_t type,
                               gcs_seqno_t*   seqno)
{
    ck_assert_msg(size == UNKNOWN_SIZE || size == act->size,
                  "gcs_core_recv(): expected %lld, returned %zd (%s)",
                  (long long) size, act->size, strerror (-act->size));
    ck_assert_msg(act->type == type,
                  "type does not match: expected %d, got %d", type, act->type);
    ck_assert_msg(act->size <= 0 || act->out != NULL,
                  "null buffer received with positive size: %zu", act->size);

    if (act->type == GCS_ACT_STATE_REQ) return false;

    // action is ordered only if it is of type GCS_ACT_TORDERED and not an error
    if (act->seqno > 0) {
        ck_assert_msg(GCS_ACT_TORDERED == act->type,
                      "GCS_ACT_TORDERED != act->type (%d), while act->seqno: "
                      "%lld", act->type, (long long)act->seqno);
        ck_assert_msg((*seqno + 1) == act->seqno,
                      "expected seqno %lld, got %lld",
                      (long long)(*seqno + 1), (long long)act->seqno);
        *seqno = *seqno + 1;
    }

    if (NULL != buf) {
        if (GCS_ACT_TORDERED == act->type) {
            // local action buffer should not be copied
            ck_assert_msg(act->local == act->in,
                          "Received buffer ptr is not the same as sent: "
                          "%p != %p", act->in, act->local);
            ck_assert_msg(!memcmp(buf, act->out, act->size),
                          "Received buffer contents is not the same as sent: "
                          "'%s' != '%s'", buf, (char*)act->out);
        }
        else {
            ck_assert_msg(act->local != buf,
                          "Received the same buffer ptr as sent");
            ck_assert_msg(!memcmp(buf, act->out, act->size),
                          "Received buffer contents is not the same as sent");
        }
    }

    return false;
}

// Wait for recv thread to complete, perform required checks
// args: action_t, expected size, expected type
static bool CORE_RECV_END(action_t*      act,
                          const void*    buf,
                          ssize_t        size,
                          gcs_act_type_t type)
{
    {
        int ret = gu_thread_join (act->thread, NULL);
        act->thread = (gu_thread_t)-1;
        FAIL_IF(0 != ret, "Failed to join recv thread: %d (%s)",
                ret, strerror (ret));
    }

    return COMMON_RECV_CHECKS (act, (const char*)buf, size, type, &Seqno);
}

// Receive action in one call, perform required checks
// args: pointer to action_t, expected size, expected type
static bool CORE_RECV_ACT (action_t*      act,
                           const void*    buf,  // single buffer action repres.
                           ssize_t        size,
                           gcs_act_type_t type)
{
    struct gcs_act_rcvd recv_act;

    act->size  = gcs_core_recv (Core, &recv_act, GU_TIME_ETERNITY);
    act->out   = (void*)recv_act.act.buf;
    act->local = recv_act.local;
    act->type  = recv_act.act.type;
    act->seqno = recv_act.id;

    return COMMON_RECV_CHECKS (act, (const char*)buf, size, type, &Seqno);
}

// Sending always needs to be done via separate thread (uses lock-stepping)
void*
core_send_thread (void* arg)
{
    action_t* act = (action_t*)arg;

    // use seqno field to pass the return code, it is signed 8-byte integer
    act->seqno = gcs_core_send (Core, act->in, act->size, act->type);

    return (NULL);
}

// Start a thread to send an action
// args: action_t object
static bool CORE_SEND_START(action_t* act)
{
    return (0 != gu_thread_create (&act->thread, NULL,
                                   core_send_thread, act));
}

// Wait for send thread to complete, perform required checks
// args: action_t, expected return code
static bool CORE_SEND_END(action_t* act, long ret)
{
    {
        long _ret = gu_thread_join (act->thread, NULL);
        act->thread = (gu_thread_t)-1;
        ck_assert_msg(0 == _ret, "Failed to join recv thread: %ld (%s)",
                      _ret, strerror (_ret));
    }

    ck_assert_msg(ret == act->seqno,
                  "gcs_core_send(): expected %lld, returned %lld (%s)",
                  (long long) ret, (long long) act->seqno, strerror (-act->seqno));

    return false;
}

// check if configuration is the one that we expected
static long
core_test_check_conf (const gcs_act_conf_t* conf,
                      bool prim, long my_idx, long memb_num)
{
    long ret = 0;

    if ((conf->conf_id >= 0) != prim) {
        gu_error ("Expected %s conf, received %s",
                  prim ? "PRIMARY" : "NON-PRIMARY",
                  (conf->conf_id >= 0) ? "PRIMARY" : "NON-PRIMARY");
        ret = -1;
    }

    if (conf->my_idx != my_idx) {
        gu_error ("Expected my_idx = %ld, got %ld", my_idx, conf->my_idx);
        ret = -1;
    }

    if (conf->my_idx != my_idx) {
        gu_error ("Expected my_idx = %ld, got %ld", my_idx, conf->my_idx);
        ret = -1;
    }

    return ret;
}

static long
core_test_set_payload_size (ssize_t s)
{
    long          ret;
    const ssize_t arbitrary_pkt_size = s + 64; // big enough for payload to fit

    ret = gcs_core_set_pkt_size (Core, arbitrary_pkt_size);
    if (ret <= 0) {
        gu_error("set_pkt_size(%zd) returned: %ld (%s)", arbitrary_pkt_size,
                 ret, strerror (-ret));
        return ret;
    }

    ret = gcs_core_set_pkt_size (Core, arbitrary_pkt_size - ret + s);
    if (ret != s) {
        gu_error("set_pkt_size() returned: %ld instead of %zd", ret, s);
        return ret;
    }

    return 0;
}

// Initialises core and backend objects + some common tests
static inline void
core_test_init (gu::Config* config, bool bootstrap = true,
                const char* name = "core_test")
{
    long     ret;
    action_t act;

    mark_point();

    ck_assert(config != NULL);

    Core = gcs_core_create (reinterpret_cast<gu_config_t*>(config), NULL, name,
                            "aaa.bbb.ccc.ddd:xxxx", 0, 0);

    ck_assert(NULL != Core);

    Backend = gcs_core_get_backend (Core);
    ck_assert(NULL != Backend);

    Seqno = 0; // reset seqno

    ret = core_test_set_payload_size (FRAG_SIZE);
    ck_assert_msg(-EBADFD == ret, "Expected -EBADFD, got: %ld (%s)",
                  ret, strerror(-ret));

    ret = gcs_core_open (Core, "yadda-yadda", "owkmevc", 1);
    ck_assert_msg(-EINVAL == ret, "Expected -EINVAL, got %ld (%s)",
                  ret, strerror(-ret));

    ret = gcs_core_open (Core, "yadda-yadda", "dummy://", bootstrap);
    ck_assert_msg(0 == ret, "Failed to open core connection: %ld (%s)",
                  ret, strerror(-ret));

    if (!bootstrap) {
        gcs_core_send_lock_step (Core, true);
        mark_point();
        return;
    }

    // receive first configuration message
    ck_assert(!CORE_RECV_ACT (&act, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act.out, bootstrap, 0, 1));
    free (act.out);

    // this will configure backend to have desired fragment size
    ret = core_test_set_payload_size (FRAG_SIZE);
    ck_assert_msg(0 == ret, "Failed to set up the message payload size: %ld (%s)",
                  ret, strerror(-ret));

    // try to send an action to check that everything's alright
    ret = gcs_core_send (Core, act1, sizeof(act1_str), GCS_ACT_TORDERED);
    ck_assert_msg(ret == sizeof(act1_str), "Expected %zu, got %ld (%s)",
                  sizeof(act1_str), ret, strerror (-ret));
    gu_warn ("Next CORE_RECV_ACT fails under valgrind");
    act.in = act1;
    ck_assert(!CORE_RECV_ACT(&act, act1_str, sizeof(act1_str),GCS_ACT_TORDERED));

    ret = gcs_core_send_join (Core, Seqno);
    ck_assert_msg(ret == 0, "gcs_core_send_join(): %ld (%s)",
                  ret, strerror(-ret));
    // no action to be received (we're joined already)

    ret = gcs_core_send_sync (Core, Seqno);
    ck_assert_msg(ret == 0, "gcs_core_send_sync(): %ld (%s)",
                  ret, strerror(-ret));
    ck_assert(!CORE_RECV_ACT(&act,NULL,sizeof(gcs_seqno_t),GCS_ACT_SYNC));
    ck_assert(Seqno == gcs_seqno_gtoh(*(gcs_seqno_t*)act.out));

    gcs_core_send_lock_step (Core, true);
    mark_point();
}

// cleans up core and backend objects
static inline void
core_test_cleanup ()
{
    long      ret;
    char      tmp[1];
    action_t  act;

    ck_assert(NULL != Core);
    ck_assert(NULL != Backend);

    // to fetch self-leave message
    ck_assert(!CORE_RECV_START (&act));
    ret = gcs_core_close (Core);
    ck_assert_msg(0 == ret, "Failed to close core: %ld (%s)",
                  ret, strerror (-ret));
    ret = CORE_RECV_END (&act, NULL, UNKNOWN_SIZE, GCS_ACT_CONF);
    ck_assert_msg(0 == ret, "ret: %ld (%s)", ret, strerror(-ret));
    free (act.out);

    // check that backend is closed too
    ret = Backend->send (Backend, tmp, sizeof(tmp), GCS_MSG_ACTION);
    ck_assert(ret == -EBADFD);

    ret = gcs_core_destroy (Core);
    ck_assert_msg(0 == ret, "Failed to destroy core: %ld (%s)",
                  ret, strerror (-ret));

    {
        ssize_t   allocated;
        allocated = gcs_tests_get_allocated();
        ck_assert_msg(0 == allocated,
                      "Expected 0 allocated bytes, found %zd", allocated);
    }
}

// just a smoke test for core API
START_TEST (gcs_core_test_api)
{
    gu::Config config;
    core_test_init (&config);
    ck_assert(NULL != Core);
    ck_assert(NULL != Backend);

    long     ret;
    long     tout = 100; // 100 ms timeout
    const struct gu_buf* act = act3;
    const void* act_buf  = act3_str;
    size_t      act_size = sizeof(act3_str);

    action_t act_s(act, NULL, NULL, act_size, GCS_ACT_TORDERED, -1, (gu_thread_t)-1);
    action_t act_r(act, NULL, NULL, -1, (gcs_act_type_t)-1, -1, (gu_thread_t)-1);
    long i = 5;

    // test basic fragmentaiton
    while (i--) {
        long     frags    = (act_size - 1)/FRAG_SIZE + 1;

        gu_info ("Iteration %ld: act: %s, size: %zu, frags: %ld",
                 i, act, act_size, frags);

        ck_assert(!CORE_SEND_START (&act_s));

        while ((ret = gcs_core_send_step (Core, 3*tout)) > 0) {
            frags--; gu_info ("frags: %ld", frags);
//            usleep (1000);
        }

        ck_assert_msg(ret == 0, "gcs_core_send_step() returned: %ld (%s)",
                      ret, strerror(-ret));
        ck_assert_msg(frags == 0, "frags = %ld, instead of 0", frags);
        ck_assert(!CORE_SEND_END (&act_s, act_size));
        ck_assert(!CORE_RECV_ACT (&act_r, act_buf, act_size, GCS_ACT_TORDERED));

        ret = gcs_core_set_last_applied (Core, Seqno);
        ck_assert_msg(ret == 0, "gcs_core_set_last_applied(): %ld (%s)",
                      ret, strerror(-ret));
        ck_assert(!CORE_RECV_ACT(&act_r, NULL, sizeof(gcs_seqno_t),
                                GCS_ACT_COMMIT_CUT));
        ck_assert(Seqno == gcs_seqno_gtoh(*(gcs_seqno_t*)act_r.out));
        free (act_r.out);
    }

    // send fake flow control action, its contents is not important
    gcs_core_send_fc (Core, act, act_size);
    ck_assert_msg(ret == 0, "gcs_core_send_fc(): %ld (%s)",
                  ret, strerror(-ret));
    ck_assert(!CORE_RECV_ACT(&act_r, act, act_size, GCS_ACT_FLOW));

    core_test_cleanup ();
}
END_TEST

// do a single send step, compare with the expected result
static inline bool
CORE_SEND_STEP (gcs_core_t* core, long timeout, long ret)
{
   long err = gcs_core_send_step (core, timeout);
   ck_assert_msg(err >= 0, "gcs_core_send_step(): %ld (%s)",
                 err, strerror (-err));
   if (ret >= 0) {
       ck_assert_msg(err == ret, "gcs_core_send_step(): expected %ld, got %ld",
                     ret, err);
   }

   return false;
}

static bool
DUMMY_INJECT_COMPONENT (gcs_backend_t* backend, const gcs_comp_msg_t* comp)
{
    long ret = gcs_dummy_inject_msg (Backend, comp,
                                     gcs_comp_msg_size(comp),
                                     GCS_MSG_COMPONENT, GCS_SENDER_NONE);
    ck_assert_msg(ret > 0, "gcs_dummy_inject_msg(): %ld (%s)",
                  ret, strerror(ret));

    return false;
}

static bool
DUMMY_INSTALL_COMPONENT (gcs_backend_t* backend, const gcs_comp_msg_t* comp)
{
    bool primary = gcs_comp_msg_primary (comp);
    long my_idx  = gcs_comp_msg_self    (comp);
    long members = gcs_comp_msg_num     (comp);

    action_t act;

    ck_assert(!gcs_dummy_set_component(Backend, comp));
    ck_assert(!DUMMY_INJECT_COMPONENT (Backend, comp));
    ck_assert(!CORE_RECV_ACT (&act, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act.out, primary,
                                     my_idx, members));
    free (act.out);
    return false;
}

START_TEST (gcs_core_test_own)
{
    long const tout = 1000; // 100 ms timeout

    const struct gu_buf* act      = act2;
    const void*          act_buf  = act2_str;
    size_t               act_size = sizeof(act2_str);

    action_t act_s(act, NULL, NULL, act_size, GCS_ACT_TORDERED, -1, (gu_thread_t)-1);
    action_t act_r(act, NULL, NULL, -1, (gcs_act_type_t)-1, -1, (gu_thread_t)-1);

    // Create primary and non-primary component messages
    gcs_comp_msg_t* prim     = gcs_comp_msg_new (true, false,  0, 1, 0);
    gcs_comp_msg_t* non_prim = gcs_comp_msg_new (false, false, 0, 1, 0);
    ck_assert(NULL != prim);
    ck_assert(NULL != non_prim);
    gcs_comp_msg_add (prim,     "node1", 0);
    gcs_comp_msg_add (non_prim, "node1", 1);

    gu::Config config;
    core_test_init (&config);

    /////////////////////////////////////////////
    /// check behaviour in transitional state ///
    /////////////////////////////////////////////

    ck_assert(!CORE_RECV_START (&act_r));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    usleep (10000); // resolve race between sending and setting transitional
    gcs_dummy_set_transitional (Backend);
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    ck_assert(!CORE_SEND_STEP (Core, tout, 0)); // no frags left
    ck_assert(NULL == act_r.out); // should not have received anything
    ck_assert(!gcs_dummy_set_component (Backend, prim)); // return to PRIM state
    ck_assert(!CORE_SEND_END (&act_s, act_size));
    ck_assert(!CORE_RECV_END (&act_r, act_buf, act_size, GCS_ACT_TORDERED));

    /*
     * TEST CASE 1: Action was sent successfully, but NON_PRIM component
     * happened before any fragment could be delivered.
     * EXPECTED OUTCOME: action is received with -ENOTCONN instead of global
     * seqno
     */
    ck_assert(!DUMMY_INJECT_COMPONENT (Backend, non_prim));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    ck_assert(!CORE_SEND_END (&act_s, act_size));
    ck_assert(!gcs_dummy_set_component(Backend, non_prim));
    ck_assert(!CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, false, 0, 1));
    free (act_r.out);
    ck_assert(!CORE_RECV_ACT (&act_r, act_buf, act_size, GCS_ACT_TORDERED));
    ck_assert_msg(-ENOTCONN == act_r.seqno,
                  "Expected -ENOTCONN, received %ld (%s)",
                  act_r.seqno, strerror (-act_r.seqno));

    /*
     * TEST CASE 2: core in NON_PRIM state. There is attempt to send an
     * action.
     * EXPECTED OUTCOME: CORE_SEND_END should return -ENOTCONN after 1st
     * fragment send fails.
     */
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    ck_assert(!CORE_SEND_STEP (Core, tout, 0)); // bail out after 1st frag
    ck_assert(!CORE_SEND_END (&act_s, -ENOTCONN));

    /*
     * TEST CASE 3: Backend in NON_PRIM state. There is attempt to send an
     * action.
     * EXPECTED OUTCOME: CORE_SEND_END should return -ENOTCONN after 1st
     * fragment send fails.
     */
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, prim));
    ck_assert(!gcs_dummy_set_component(Backend, non_prim));
    ck_assert(!DUMMY_INJECT_COMPONENT (Backend, non_prim));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    ck_assert(!CORE_SEND_END (&act_s, -ENOTCONN));
    ck_assert(!CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, false, 0, 1));
    free (act_r.out);

    /*
     * TEST CASE 4: Action was sent successfully, but NON_PRIM component
     * happened in between delivered fragments.
     * EXPECTED OUTCOME: action is received with -ENOTCONN instead of global
     * seqno.
     */
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, prim));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    ck_assert(!DUMMY_INJECT_COMPONENT (Backend, non_prim));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    ck_assert(!CORE_SEND_END (&act_s, act_size));
    ck_assert(!CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, false, 0, 1));
    free (act_r.out);
    ck_assert(!CORE_RECV_ACT (&act_r, act_buf, act_size, GCS_ACT_TORDERED));
    ck_assert_msg(-ENOTCONN == act_r.seqno,
                  "Expected -ENOTCONN, received %ld (%s)",
                  act_r.seqno, strerror (-act_r.seqno));

    /*
     * TEST CASE 5: Action is being sent and received concurrently. In between
     * two fragments recv thread receives NON_PRIM and then PRIM components.
     * EXPECTED OUTCOME: CORE_RECV_ACT should receive the action with -ERESTART
     * instead of seqno.
     */
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, prim));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    usleep (100000); // make sure 1st fragment gets in before new component
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, non_prim));
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, prim));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    ck_assert(!CORE_SEND_END (&act_s, act_size));
    ck_assert(!CORE_RECV_ACT (&act_r, act_buf, act_size, GCS_ACT_TORDERED));
    ck_assert_msg(-ERESTART == act_r.seqno,
                  "Expected -ERESTART, received %ld (%s)",
                  act_r.seqno, strerror (-act_r.seqno));

    /*
     * TEST CASE 6: Action has 3 fragments, 2 were sent successfully but the
     * 3rd failed because backend is in NON_PRIM. In addition NON_PRIM component
     * happened in between delivered fragments.
     * subcase 1: new component received first
     * subcase 2: 3rd fragment is sent first
     * EXPECTED OUTCOME: CORE_SEND_END should return -ENOTCONN after 3rd
     * fragment send fails.
     */
    act        = act3;
    act_buf    = act3_str;
    act_size   = sizeof(act3_str);
    act_s.in   = act;
    act_s.size = act_size;

    // subcase 1
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, prim));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    ck_assert(!DUMMY_INJECT_COMPONENT (Backend, non_prim));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    usleep (500000); // fail_if_seq
    ck_assert(!gcs_dummy_set_component(Backend, non_prim));
    ck_assert(!CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, false, 0, 1));
    free (act_r.out);
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 3rd frag
    ck_assert(!CORE_SEND_END (&act_s, -ENOTCONN));

    // subcase 2
    ck_assert(!DUMMY_INSTALL_COMPONENT (Backend, prim));
    ck_assert(!CORE_SEND_START (&act_s));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    ck_assert(!DUMMY_INJECT_COMPONENT (Backend, non_prim));
    ck_assert(!CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    usleep (1000000);
    ck_assert(!gcs_dummy_set_component(Backend, non_prim));
    ck_assert(!CORE_SEND_STEP (Core, 4*tout, 1)); // 3rd frag
    ck_assert(!CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, false, 0, 1));
    free (act_r.out);
    ck_assert(!CORE_SEND_END (&act_s, -ENOTCONN));

    gu_free (prim);
    gu_free (non_prim);

    core_test_cleanup ();
}
END_TEST

#ifdef GCS_ALLOW_GH74
/*
 * Disabled test because it is too slow and timeouts on crowded
 * build systems like e.g. build.opensuse.org */

START_TEST (gcs_core_test_gh74)
{
    gu::Config config;
    core_test_init(&config, true, "node1");

    // set frag size large enough to avoid fragmentation.
    gu_info ("set payload size = 1024");
    core_test_set_payload_size(1024);

    // new primary comp message.
    gcs_comp_msg_t* prim = gcs_comp_msg_new (true, false, 0, 2, 0);
    ck_assert(NULL != prim);
    gcs_comp_msg_add(prim, "node1", 0);
    gcs_comp_msg_add(prim, "node2", 1);

    // construct state transform request.
    static const char* req_ptr = "12345";
    static const size_t req_size = 6;
    static const char* donor = ""; // from *any*
    static const size_t donor_len = strlen(donor) + 1;
    size_t act_size = req_size + donor_len;
    char* act_ptr = 0;

    act_ptr = (char*)gu_malloc(act_size);
    memcpy(act_ptr, donor, donor_len);
    memcpy(act_ptr + donor_len, req_ptr, req_size);

    // serialize request into message.
    gcs_act_frag_t frg;
    frg.proto_ver = gcs_core_group_protocol_version(Core);
    frg.frag_no = 0;
    frg.act_id = 1;
    frg.act_size = act_size;
    frg.act_type = GCS_ACT_STATE_REQ;
    char msg_buf[1024];
    ck_assert(!gcs_act_proto_write(&frg, msg_buf, sizeof(msg_buf)));
    memcpy(const_cast<void*>(frg.frag), act_ptr, act_size);
    size_t msg_size = act_size + gcs_act_proto_hdr_size(frg.proto_ver);
    // gu_free(act_ptr);

    // state exchange message.
    gu_uuid_t state_uuid;
    gu_uuid_generate(&state_uuid, NULL, 0);
    gcs_core_set_state_uuid(Core, &state_uuid);

    // construct uuid message from node1.
    size_t uuid_len = sizeof(state_uuid);
    char uuid_buf[uuid_len];
    memcpy(uuid_buf, &state_uuid, uuid_len);

    gcs_state_msg_t* state_msg = NULL;
    const gcs_group_t* group = gcs_core_get_group(Core);

    // state exchange message from node1
    state_msg = gcs_group_get_state(group);
    state_msg->state_uuid = state_uuid;
    size_t state_len = gcs_state_msg_len (state_msg);
    char state_buf[state_len];
    gcs_state_msg_write (state_buf, state_msg);
    gcs_state_msg_destroy (state_msg);

    // state exchange message from node2
    state_msg = gcs_state_msg_create(&state_uuid,
                                     &GU_UUID_NIL,
                                     &GU_UUID_NIL,
                                     GCS_SEQNO_ILL,
                                     GCS_SEQNO_ILL,
                                     GCS_SEQNO_ILL,
                                     0,
                                     GCS_NODE_STATE_NON_PRIM,
                                     GCS_NODE_STATE_PRIM,
                                     "node2", "127.0.0.1",
                                     group->gcs_proto_ver,
                                     group->repl_proto_ver,
                                     group->appl_proto_ver,
                                     group->prim_gcs_ver,
                                     group->prim_repl_ver,
                                     group->prim_appl_ver,
                                     0, // desync count
                                     0);
    size_t state_len2 = gcs_state_msg_len (state_msg);
    char state_buf2[state_len2];
    gcs_state_msg_write (state_buf2, state_msg);
    gcs_state_msg_destroy (state_msg);

    action_t act_r(NULL,  NULL, NULL, -1, (gcs_act_type_t)-1, -1, (gu_thread_t)-1);

    // ========== from node1's view ==========
    ck_assert(!gcs_dummy_set_component(Backend, prim));
    ck_assert(!DUMMY_INJECT_COMPONENT(Backend, prim));
    gu_free(prim);
    CORE_RECV_START(&act_r); // we have to start another thread here.
    // otherwise messages to node1 can not be in right order.
    for(;;) {
        usleep(10000); // make sure node1 already changed its status to WAIT_STATE_MSG
        if (gcs_group_state(group) == GCS_GROUP_WAIT_STATE_MSG) {
            break;
        }
    }
    // then STR sneaks before new configuration is delivered.
    ck_assert(gcs_dummy_inject_msg(Backend, msg_buf, msg_size,
                                   GCS_MSG_ACTION, 1) == (int)msg_size);
    // then state exchange message from node2.
    ck_assert(gcs_dummy_inject_msg(Backend, state_buf2, state_len2,
                                   GCS_MSG_STATE_MSG, 1) == (int)state_len2);
    // expect STR is lost here.
    ck_assert(!CORE_RECV_END(&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, true, 0, 2));
    free(act_r.out);
    core_test_cleanup();

    // ========== from node2's view ==========
    core_test_init(&config, false, "node2");

    // set frag size large enough to avoid fragmentation.
    gu_info ("set payload size = 1024");
    core_test_set_payload_size(1024);

    prim = gcs_comp_msg_new (true, false, 1, 2, 0);
    ck_assert(NULL != prim);
    gcs_comp_msg_add(prim, "node1", 0);
    gcs_comp_msg_add(prim, "node2", 1);

    // node1 and node2 joins.
    // now node2's status == GCS_NODE_STATE_PRIM
    ck_assert(!gcs_dummy_set_component(Backend, prim));
    ck_assert(!DUMMY_INJECT_COMPONENT(Backend, prim));
    gu_free(prim);
    ck_assert(gcs_dummy_inject_msg(Backend, uuid_buf, uuid_len,
                                   GCS_MSG_STATE_UUID, 0) == (int)uuid_len);
    ck_assert(gcs_dummy_inject_msg(Backend, state_buf, state_len,
                                   GCS_MSG_STATE_MSG, 0) == (int)state_len);
    ck_assert(!CORE_RECV_ACT(&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out, true, 1, 2));
    free(act_r.out);

    // then node3 joins.
    prim = gcs_comp_msg_new (true, false, 1, 3, 0);
    ck_assert(NULL != prim);
    gcs_comp_msg_add(prim, "node1", 0);
    gcs_comp_msg_add(prim, "node2", 1);
    gcs_comp_msg_add(prim, "node3", 2);
    ck_assert(!gcs_dummy_set_component(Backend, prim));
    ck_assert(!DUMMY_INJECT_COMPONENT(Backend, prim));
    gu_free(prim);

    // generate a new state uuid.
    gu_uuid_generate(&state_uuid, NULL, 0);
    memcpy(uuid_buf, &state_uuid, uuid_len);

    // state exchange message from node3
    group = gcs_core_get_group(Core);
    state_msg = gcs_state_msg_create(&state_uuid,
                                     &GU_UUID_NIL,
                                     &GU_UUID_NIL,
                                     GCS_SEQNO_ILL,
                                     GCS_SEQNO_ILL,
                                     GCS_SEQNO_ILL,
                                     0,
                                     GCS_NODE_STATE_NON_PRIM,
                                     GCS_NODE_STATE_PRIM,
                                     "node3", "127.0.0.1",
                                     group->gcs_proto_ver,
                                     group->repl_proto_ver,
                                     group->appl_proto_ver,
                                     group->prim_gcs_ver,
                                     group->prim_repl_ver,
                                     group->prim_appl_ver,
                                     0, // desync count
                                     0);
    size_t state_len3 = gcs_state_msg_len (state_msg);
    char state_buf3[state_len3];
    gcs_state_msg_write (state_buf3, state_msg);
    gcs_state_msg_destroy (state_msg);

    // updating state message from node1.
    group = gcs_core_get_group(Core);
    state_msg = gcs_group_get_state(group);
    state_msg->flags = GCS_STATE_FREP | GCS_STATE_FCLA;
    state_msg->prim_state = GCS_NODE_STATE_JOINED;
    state_msg->current_state = GCS_NODE_STATE_SYNCED;
    state_msg->state_uuid = state_uuid;
    state_msg->name = "node1";
    gcs_state_msg_write(state_buf, state_msg);
    gcs_state_msg_destroy(state_msg);

    ck_assert(gcs_dummy_inject_msg(Backend, uuid_buf, uuid_len,
                                   GCS_MSG_STATE_UUID, 0) == (int)uuid_len);
    ck_assert(gcs_dummy_inject_msg(Backend, state_buf, state_len,
                                   GCS_MSG_STATE_MSG, 0) == (int)state_len);

    // STR sneaks.
    // we have to make same message exists in sender queue too.
    // otherwise we will get following log
    // "FIFO violation: queue empty when local action received"
    const struct gu_buf act = {act_ptr, (ssize_t)act_size};
    action_t act_s(&act, NULL, NULL, act_size, GCS_ACT_STATE_REQ, -1, (gu_thread_t)-1);
    CORE_SEND_START(&act_s);
    for(;;) {
        usleep(10000);
        gcs_fifo_lite_t* fifo = gcs_core_get_fifo(Core);
        void* item = gcs_fifo_lite_get_head(fifo);
        if (item) {
            gcs_fifo_lite_release(fifo);
            break;
        }
    }
    ck_assert(gcs_dummy_inject_msg(Backend, msg_buf, msg_size,
                                   GCS_MSG_ACTION, 1) == (int)msg_size);

    ck_assert(gcs_dummy_inject_msg(Backend, state_buf3, state_len3,
                                   GCS_MSG_STATE_MSG, 2) == (int)state_len3);

    // expect STR and id == -EAGAIN.
    ck_assert(!CORE_RECV_ACT(&act_r, act_ptr, act_size, GCS_ACT_STATE_REQ));
    ck_assert(act_r.seqno == -EAGAIN);
    free(act_r.out);

    ck_assert(!CORE_RECV_ACT(&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    ck_assert(!core_test_check_conf((const gcs_act_conf_t*)act_r.out,true,1,3));
    free(act_r.out);

    // core_test_cleanup();
    // ==========
    gu_free(act_ptr);
}
END_TEST
#endif /* GCS_ALLOW_GH74 */


#if 0 // requires multinode support from gcs_dummy
START_TEST (gcs_core_test_foreign)
{
    core_test_init ();

    core_test_cleanup ();
}
END_TEST
#endif // 0

Suite *gcs_core_suite(void)
{
  Suite *suite = suite_create("GCS core context");
  TCase *tcase = tcase_create("gcs_core");

  suite_add_tcase (suite, tcase);
  tcase_set_timeout(tcase, 60);
  // Tests in this suite leak memory, disable them for now if ASAN
  // is enabled.
#ifdef GALERA_WITH_ASAN
  bool skip = true;
#else
  bool skip = false;
#endif // GALERA_WITH_ASAN

  if (skip == false) {
      tcase_add_test  (tcase, gcs_core_test_api);
      tcase_add_test  (tcase, gcs_core_test_own);
#ifdef GCS_ALLOW_GH74
      tcase_add_test  (tcase, gcs_core_test_gh74);
#endif /* GCS_ALLOW_GH74 */
      // tcase_add_test (tcase, gcs_core_test_foreign);
  }
  return suite;
}
