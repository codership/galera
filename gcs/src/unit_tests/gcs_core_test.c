/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
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
 * In the second apporach messages will wait in queue and be fetched afterwards
 *
 */
#include <check.h>
#include <errno.h>
#include <stdlib.h>

#include <galerautils.h>

#include "../gcs_core.h"
#include "../gcs_dummy.h"
#include "gcs_core_test.h"

extern ssize_t gcs_tests_get_allocated();

static const long UNKNOWN_SIZE = 1234567890; // some unrealistic number

static gcs_core_t*    Core    = NULL;
static gcs_backend_t* Backend = NULL;
static gcs_seqno_t    Seqno   = 0;

typedef struct action {
    const void*    data;
    ssize_t        size;
    gcs_act_type_t type;
    gcs_seqno_t    seqno;
    gu_thread_t    thread;
} action_t;

//static struct action_t RecvAct;
static const ssize_t FRAG_SIZE = 4; // desirable action fragment size
static const char act1[] = "101";         // 1-fragment action
static const char act2[] = "202122";      // 2-fragment action
static const char act3[] = "3031323334";  // 3-fragment action

/*
 * Huge macros which follow below cannot be functions for the purpose
 * of correct line reporting.
 */

// action receive thread, returns after first action received, stores action
// in the passed action_t object, uses global Core to receive
static void*
core_recv_thread (void* arg)
{
    action_t* act = arg;

    // @todo: refactor according to new gcs_act types
    struct gcs_act_rcvd recv_act;
    bool is_local;

    act->size = gcs_core_recv (Core, &recv_act, &is_local, GU_TIME_ETERNITY);
    act->data = recv_act.act.buf;
    act->type = recv_act.act.type;
    act->seqno = recv_act.id;

    return (NULL);
}

// this macro logs errors from within a function
#define FAIL_IF(expr, format, ...)                 \
    if (expr) {                                    \
        gu_error(format, ## __VA_ARGS__, NULL);    \
        return true;                               \
    }

// Start a thread to receive an action
// args: action_t object
static inline bool CORE_RECV_START(action_t* act)
{
    return (0 != gu_thread_create (&act->thread, NULL,
                                   core_recv_thread, act));
}

static bool COMMON_RECV_CHECKS(action_t*      act,
                               const void*    buf,
                               ssize_t        size,
                               gcs_act_type_t type,
                               gcs_seqno_t*   seqno)
{
    FAIL_IF (size != UNKNOWN_SIZE && size != act->size,
             "gcs_core_recv(): expected %lld, returned %zd (%s)",
             (long long) size, act->size, strerror (-act->size));
    FAIL_IF (act->type != type,
             "type does not match: expected %d, got %d", type, act->type);
    FAIL_IF (act->size > 0 && act->data == NULL,
             "null buffer with positive size: %zu", act->size);

    // action is ordered only if it is of type GCS_ACT_TORDERED and not an error
    if (act->seqno >= GCS_SEQNO_NIL) {
        FAIL_IF (GCS_ACT_TORDERED != act->type,
                 "GCS_ACT_TORDERED != act->type (%d), while act->seqno: %lld",
                 act->type, (long long)act->seqno);
        FAIL_IF ((*seqno + 1) != act->seqno,
                 "expected seqno %lld, got %lld",
                 (long long)(*seqno + 1), (long long)act->seqno);
        *seqno = *seqno + 1;
    }

    if (NULL != buf) {
        if (GCS_ACT_TORDERED == act->type) {
            // local action buffer should not be copied
            FAIL_IF (act->data != buf,
                     "Received buffer ptr is not the same as sent");
        }
        else {
            FAIL_IF (act->data == buf,
                     "Received the same buffer ptr as sent");
            FAIL_IF (memcmp (buf, act->data, act->size),
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
        act->thread = -1;
        FAIL_IF(0 != ret, "Failed to join recv thread: %ld (%s)",
                ret, strerror (ret));
    }

    return COMMON_RECV_CHECKS (act, buf, size, type, &Seqno);
}

// Receive action in one call, perform required checks
// args: pointer to action_t, expected size, expected type
static bool CORE_RECV_ACT (action_t*      act,
                           const void*    buf,
                           ssize_t        size,
                           gcs_act_type_t type)
{
    struct gcs_act_rcvd recv_act;
    bool is_local;

    act->size  = gcs_core_recv (Core, &recv_act, &is_local, GU_TIME_ETERNITY);
    act->data  = recv_act.act.buf;
    act->type  = recv_act.act.type;
    act->seqno = recv_act.id;

    return COMMON_RECV_CHECKS (act, buf, size, type, &Seqno);
}

// Sending always needs to be done via separate thread (uses lock-stepping)
void*
core_send_thread (void* arg)
{
    action_t* act = arg;

    // use seqno field to pass the return code, it is signed 8-byte integer
    act->seqno = gcs_core_send (Core, act->data, act->size, act->type);

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
        act->thread = -1;
        FAIL_IF (0 != _ret, "Failed to join recv thread: %ld (%s)",
                 _ret, strerror (_ret));
    }

    FAIL_IF (ret != act->seqno,
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
core_test_init ()
{
    long     ret;
    action_t act;

    mark_point();

    gu_config_t* config = gu_config_create ("");
    fail_if (config == NULL);

    Core = gcs_core_create ("core_test",
                            "aaa.bbb.ccc.ddd:xxxx", config, 0, 0);

    fail_if (NULL == Core);

    Backend = gcs_core_get_backend (Core);
    fail_if (NULL == Backend);

    Seqno = 0; // reset seqno

    ret = core_test_set_payload_size (FRAG_SIZE);
    fail_if (-EBADFD != ret, "Expected -EBADFD, got: %ld (%s)",
             ret, strerror(-ret));

    ret = gcs_core_open (Core, "yadda-yadda", "owkmevc");
    fail_if (-EINVAL != ret, "Expected -EINVAL, got %ld (%s)",
             ret, strerror(-ret));

    ret = gcs_core_open (Core, "yadda-yadda", "dummy://");
    fail_if (0 != ret, "Failed to open core connection: %ld (%s)",
             ret, strerror(-ret));

    // this will configure backend to have desired fragment size
    ret = core_test_set_payload_size (FRAG_SIZE);
    fail_if (0 != ret, "Failed to set up the message payload size: %ld (%s)",
             ret, strerror(-ret));

    // receive first configuration message
    fail_if (CORE_RECV_ACT (&act, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    fail_if (core_test_check_conf(act.data, true, 0, 1));
    free ((void*)act.data);

    // try to send an action to check that everything's alright
    ret = gcs_core_send (Core, act1, sizeof(act1), GCS_ACT_TORDERED);
    fail_if (ret != sizeof(act1), "Expected %d, got %d (%s)",
             sizeof(act1), ret, strerror (-ret));
    gu_warn ("Next CORE_RECV_ACT fails under valgrind");
    fail_if (CORE_RECV_ACT (&act, act1, sizeof(act1), GCS_ACT_TORDERED));

    ret = gcs_core_send_join (Core, Seqno);
    fail_if (ret != 0, "gcs_core_send_join(): %ld (%s)",
             ret, strerror(-ret));
    // no action to be received (we're joined already)
    
    ret = gcs_core_send_sync (Core, Seqno);
    fail_if (ret != 0, "gcs_core_send_sync(): %ld (%s)",
             ret, strerror(-ret));
    fail_if (CORE_RECV_ACT(&act,NULL,sizeof(gcs_seqno_t),GCS_ACT_SYNC));
    fail_if (Seqno != *(gcs_seqno_t*)act.data);

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

    fail_if (NULL == Core);
    fail_if (NULL == Backend);

    // to fetch self-leave message
    fail_if (CORE_RECV_START (&act));
    ret = gcs_core_close (Core);
    fail_if (0 != ret, "Failed to close core: %ld (%s)",
             ret, strerror (-ret));
    fail_if (CORE_RECV_END   (&act, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    free ((void*)act.data);

    // check that backend is closed too
    ret = Backend->send (Backend, tmp, sizeof(tmp), GCS_MSG_ACTION);
    fail_if (ret != -EBADFD);

    ret = gcs_core_destroy (Core);
    fail_if (0 != ret, "Failed to destroy core: %ld (%s)",
             ret, strerror (-ret));

    {
        ssize_t   allocated;
        allocated = gcs_tests_get_allocated();
        fail_if (0 != allocated,
                 "Expected 0 allocated bytes, found %zd", allocated);
    }
}

// just a smoke test for core API
START_TEST (gcs_core_test_api)
{
#define ACT act3
    long     ret;
    long     tout = 100; // 100 ms timeout
    size_t   act_size = sizeof(ACT);
    action_t act_s    = { ACT, act_size, GCS_ACT_TORDERED, -1, -1 };
    action_t act_r;
    long i = 5;

    core_test_init ();
    fail_if (NULL == Core);
    fail_if (NULL == Backend);

    // test basic fragmentaiton
    while (i--) {
        long     frags    = (act_size - 1)/FRAG_SIZE + 1;

        gu_info ("Iteration %ld: act: %s, size: %zu, frags: %ld",
                 i, ACT, act_size, frags);

        fail_if (CORE_SEND_START (&act_s));

        while ((ret = gcs_core_send_step (Core, 3*tout)) > 0) {
            frags--; gu_info ("frags: %ld", frags);
//            usleep (1000);
        }

        fail_if (ret != 0, "gcs_core_send_step() returned: %ld (%s)",
                 ret, strerror(-ret));
        fail_if (frags != 0, "frags = %ld, instead of 0", frags);
        fail_if (CORE_SEND_END (&act_s, act_size));
        fail_if (CORE_RECV_ACT (&act_r, ACT, act_size, GCS_ACT_TORDERED));

        ret = gcs_core_set_last_applied (Core, Seqno);
        fail_if (ret != 0, "gcs_core_set_last_applied(): %ld (%s)",
                 ret, strerror(-ret));
        fail_if (CORE_RECV_ACT (&act_r, NULL, sizeof(gcs_seqno_t),
                                GCS_ACT_COMMIT_CUT));
        fail_if (Seqno != *(gcs_seqno_t*)act_r.data);
        free ((void*)act_r.data);
    }

    // send fake flow control action, its contents is not important
    gcs_core_send_fc (Core, ACT, act_size);
    fail_if (ret != 0, "gcs_core_send_fc(): %ld (%s)",
             ret, strerror(-ret));
    fail_if (CORE_RECV_ACT(&act_r, ACT, act_size, GCS_ACT_FLOW));

    core_test_cleanup ();
}
END_TEST

// do a single send step, compare with the expected result
static inline bool
CORE_SEND_STEP (gcs_core_t* core, long timeout, long ret)
{
   long err = gcs_core_send_step (core, timeout);
   FAIL_IF (err < 0, "gcs_core_send_step(): %ld (%s)",
            err, strerror (-err));
   if (ret >= 0) {
       FAIL_IF (err != ret, "gcs_core_send_step(): expected %ld, got %ld",
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
    FAIL_IF (ret <= 0, "gcs_dummy_inject_msg(): %ld (%s)", ret, strerror(ret));

    return false;
}

static bool
DUMMY_INSTALL_COMPONENT (gcs_backend_t* backend, const gcs_comp_msg_t* comp)
{
    bool primary = gcs_comp_msg_primary (comp);
    long my_idx  = gcs_comp_msg_self    (comp);
    long members = gcs_comp_msg_num     (comp);

    action_t act;

    FAIL_IF (gcs_dummy_set_component(Backend, comp), "");
    FAIL_IF (DUMMY_INJECT_COMPONENT (Backend, comp), "");
    FAIL_IF (CORE_RECV_ACT (&act, NULL, UNKNOWN_SIZE, GCS_ACT_CONF), "");
    FAIL_IF (core_test_check_conf(act.data, primary, my_idx, members), "");
    free ((void*)act.data);
    return false;
}

START_TEST (gcs_core_test_own)
{
#undef ACT
#define ACT act2
    long     tout = 100; // 100 ms timeout
    size_t   act_size = sizeof(ACT);
    action_t act_s    = { ACT, act_size, GCS_ACT_TORDERED, -1, -1 };
    action_t act_r    = { NULL, -1, -1, -1, -1 };

    // Create primary and non-primary component messages
    gcs_comp_msg_t* prim     = gcs_comp_msg_new (true,  0, 1);
    gcs_comp_msg_t* non_prim = gcs_comp_msg_new (false, 0, 1);
    fail_if (NULL == prim);
    fail_if (NULL == non_prim);
    gcs_comp_msg_add (prim,     "node1");
    gcs_comp_msg_add (non_prim, "node1");

    core_test_init ();

    /////////////////////////////////////////////
    /// check behaviour in transitional state ///
    /////////////////////////////////////////////

    fail_if (CORE_RECV_START (&act_r));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    usleep (10000); // resolve race between sending and setting transitional
    gcs_dummy_set_transitional (Backend);
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    fail_if (CORE_SEND_STEP (Core, tout, 0)); // no frags left
    fail_if (NULL != act_r.data); // should not have received anything
    fail_if (gcs_dummy_set_component (Backend, prim)); // return to PRIM state
    fail_if (CORE_SEND_END (&act_s, act_size));
    fail_if (CORE_RECV_END (&act_r, ACT, act_size, GCS_ACT_TORDERED));

    /*
     * TEST CASE 1: Action was sent successfully, but NON_PRIM component
     * happened before any fragment could be delivered.
     * EXPECTED OUTCOME: action is received with -ENOTCONN instead of global
     * seqno
     */
    fail_if (DUMMY_INJECT_COMPONENT (Backend, non_prim));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    fail_if (CORE_SEND_END (&act_s, act_size));
    fail_if (gcs_dummy_set_component(Backend, non_prim));
    fail_if (CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    fail_if (core_test_check_conf(act_r.data, false, 0, 1));
    free ((void*)act_r.data);
    fail_if (CORE_RECV_ACT (&act_r, ACT, act_size, GCS_ACT_TORDERED));
    fail_if (-ENOTCONN != act_r.seqno, "Expected -ENOTCONN, received %ld (%s)",
             act_r.seqno, strerror (-act_r.seqno));

    /*
     * TEST CASE 2: core in NON_PRIM state. There is attempt to send an
     * action.
     * EXPECTED OUTCOME: CORE_SEND_END should return -ENOTCONN after 1st
     * fragment send fails.
     */
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    fail_if (CORE_SEND_STEP (Core, tout, 0)); // bail out after 1st frag
    fail_if (CORE_SEND_END (&act_s, -ENOTCONN));

    /*
     * TEST CASE 3: Backend in NON_PRIM state. There is attempt to send an
     * action.
     * EXPECTED OUTCOME: CORE_SEND_END should return -ENOTCONN after 1st
     * fragment send fails.
     */
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, prim));
    fail_if (gcs_dummy_set_component(Backend, non_prim));
    fail_if (DUMMY_INJECT_COMPONENT (Backend, non_prim));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    fail_if (CORE_SEND_END (&act_s, -ENOTCONN));
    fail_if (CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    fail_if (core_test_check_conf(act_r.data, false, 0, 1));
    free ((void*)act_r.data);

    /*
     * TEST CASE 4: Action was sent successfully, but NON_PRIM component
     * happened in between delivered fragments.
     * EXPECTED OUTCOME: action is received with -ENOTCONN instead of global
     * seqno.
     */
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, prim));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    fail_if (DUMMY_INJECT_COMPONENT (Backend, non_prim));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    fail_if (CORE_SEND_END (&act_s, act_size));
    fail_if (CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    fail_if (core_test_check_conf(act_r.data, false, 0, 1));
    free ((void*)act_r.data);
    fail_if (CORE_RECV_ACT (&act_r, ACT, act_size, GCS_ACT_TORDERED));
    fail_if (-ENOTCONN != act_r.seqno, "Expected -ENOTCONN, received %ld (%s)",
             act_r.seqno, strerror (-act_r.seqno));

    /*
     * TEST CASE 5: Action is being sent and received concurrently. In between
     * two fragments recv thread receives NON_PRIM and then PRIM components.
     * EXPECTED OUTCOME: CORE_RECV_ACT should receive the action with -ERESTART
     * instead of seqno.
     */
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, prim));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    usleep (100000); // make sure 1st fragment gets in before new component
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, non_prim));
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, prim));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    fail_if (CORE_SEND_END (&act_s, act_size));
    fail_if (CORE_RECV_ACT (&act_r, ACT, act_size, GCS_ACT_TORDERED));
    fail_if (-ERESTART != act_r.seqno, "Expected -ERESTART, received %ld (%s)",
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
#undef ACT
#define ACT act3
    act_size   = sizeof(ACT);
    act_s.data = ACT;
    act_s.size = act_size;

    // subcase 1
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, prim));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    fail_if (DUMMY_INJECT_COMPONENT (Backend, non_prim));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    usleep (500000); // fail_if_seq
    fail_if (gcs_dummy_set_component(Backend, non_prim));
    fail_if (CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    fail_if (core_test_check_conf(act_r.data, false, 0, 1));
    free ((void*)act_r.data);
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 3rd frag
    fail_if (CORE_SEND_END (&act_s, -ENOTCONN));

    // subcase 2
    fail_if (DUMMY_INSTALL_COMPONENT (Backend, prim));
    fail_if (CORE_SEND_START (&act_s));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 1st frag
    fail_if (DUMMY_INJECT_COMPONENT (Backend, non_prim));
    fail_if (CORE_SEND_STEP (Core, tout, 1)); // 2nd frag
    usleep (500000);
    fail_if (gcs_dummy_set_component(Backend, non_prim));
    fail_if (CORE_SEND_STEP (Core, 4*tout, 1)); // 3rd frag
    fail_if (CORE_RECV_ACT (&act_r, NULL, UNKNOWN_SIZE, GCS_ACT_CONF));
    fail_if (core_test_check_conf(act_r.data, false, 0, 1));
    free ((void*)act_r.data);
    fail_if (CORE_SEND_END (&act_s, -ENOTCONN));

    gu_free (prim);
    gu_free (non_prim);

    core_test_cleanup ();
}
END_TEST

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
  tcase_add_test  (tcase, gcs_core_test_api);
  tcase_add_test  (tcase, gcs_core_test_own);
//  tcase_add_test  (tcase, gcs_core_test_foreign);
  return suite;
}
