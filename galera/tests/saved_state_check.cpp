/*
 * Copyright (C) 2012-2020 Codership Oy <info@codership.com>
 */

#include "../src/saved_state.hpp"

#include "../src/uuid.hpp"

#include "gu_inttypes.hpp"

#include <check.h>
#include <errno.h>
#include <gu_threads.h>

static volatile bool stop(false);

using namespace galera;

static void*
thread_routine (void* arg)
{
    SavedState* st(static_cast<SavedState*>(arg));

    do
    {
        st->mark_unsafe();
        st->mark_safe();
    }
    while (!stop);

    return NULL;
}

static const int max_threads(16);
static gu_thread_t threads[max_threads];

static void
start_threads(void* arg)
{
    stop = false;

    for (int ret = 0; ret < max_threads; ++ret)
    {
        gu_thread_t t;
        int err = gu_thread_create (&t, NULL, thread_routine, arg);
        ck_assert_msg(0 == err, "Failed to start thread %d: %d (%s)",
                      ret, err, strerror(err));
        threads[ret] = t;
    }
}

static void
stop_threads()
{
    stop = true;

    for (int t = 0; t < max_threads; ++t)
    {
        gu_thread_join(threads[t], NULL);
    }
}

static const char* fname("grastate.dat");

START_TEST(test_basic)
{
    unlink (fname);

    wsrep_uuid_t  uuid;
    wsrep_seqno_t seqno;
    bool safe_to_bootstrap;

    {
        SavedState st(fname);

        st.get(uuid, seqno, safe_to_bootstrap);

        ck_assert(uuid  == WSREP_UUID_UNDEFINED);
        ck_assert(seqno == WSREP_SEQNO_UNDEFINED);
        ck_assert(safe_to_bootstrap == true);

        gu_uuid_from_string("b2c01654-8dfe-11e1-0800-a834d641cfb5",
                            to_gu_uuid(uuid));
        seqno = 2345234LL;

        st.set(uuid, seqno, false);
    }

    {
        SavedState st(fname);

        wsrep_uuid_t  u;
        wsrep_seqno_t s;
        bool stb;

        st.get(u, s, stb);

        ck_assert(u == uuid);
        ck_assert(s == seqno);
        ck_assert(stb == false);
    }
}
END_TEST

#define TEST_USLEEP 2500 // 2.5ms

START_TEST(test_unsafe)
{
    SavedState st(fname);

    wsrep_uuid_t  uuid;
    wsrep_seqno_t seqno;
    bool safe_to_bootstrap;

    st.get(uuid, seqno, safe_to_bootstrap);

    ck_assert(uuid  != WSREP_UUID_UNDEFINED);
    ck_assert(seqno != WSREP_SEQNO_UNDEFINED);
    ck_assert(safe_to_bootstrap != true);

    st.set(uuid, WSREP_SEQNO_UNDEFINED, false);

    for (int i = 0; i < 100; ++i)
    {
        start_threads(&st);
        mark_point();
        usleep (TEST_USLEEP);
        st.set(uuid, i, false); // make sure that state is not lost if set concurrently
        mark_point();
        usleep (TEST_USLEEP);
        stop_threads();
        mark_point();
        st.get(uuid, seqno, safe_to_bootstrap);

        ck_assert(uuid != WSREP_UUID_UNDEFINED);
        ck_assert(seqno == i);
        ck_assert(safe_to_bootstrap == false);
    }

    long marks, locks, writes;

    st.stats(marks, locks, writes);

    log_info << "Total marks: " << marks << ", total writes: " << writes
             << ", total locks: " << locks
             << "\nlocks ratio:  " << (double(locks)/marks)
             << "\nwrites ratio: " << (double(writes)/locks);
}
END_TEST

START_TEST(test_corrupt)
{
    wsrep_uuid_t  uuid;
    wsrep_seqno_t seqno;
    bool safe_to_bootstrap;

    {
        SavedState st(fname);

        st.get(uuid, seqno, safe_to_bootstrap);

        ck_assert(uuid  != WSREP_UUID_UNDEFINED);
        ck_assert(seqno != WSREP_SEQNO_UNDEFINED);
        ck_assert(safe_to_bootstrap != true);

        st.set(uuid, WSREP_SEQNO_UNDEFINED, false);
    }

    long marks(0), locks(0), writes(0);

    for (int i = 0; i < 100; ++i)
    {
        SavedState st(fname);
        // explicitly overwrite corruption mark.
        st.set (uuid, seqno, false);

        start_threads(&st);
        mark_point();
        usleep (TEST_USLEEP);
        st.mark_corrupt();
        st.set (uuid, seqno, false); // make sure that corrupt stays
        usleep (TEST_USLEEP);
        mark_point();
        stop_threads();
        mark_point();

        wsrep_uuid_t  u;
        wsrep_seqno_t s;
        bool stb;
        st.get(u, s, stb);

        // make sure that mark_corrupt() stays
        ck_assert(u == WSREP_UUID_UNDEFINED);
        ck_assert(s == WSREP_SEQNO_UNDEFINED);
        ck_assert(stb == false);

        long m, l, w;

        st.stats(m, l, w);

        marks += m;
        locks += l;
        writes += w;
    }

    log_info << "Total marks: " << marks << ", total locks: " << locks
             << ", total writes: " << writes
             << "\nlocks ratio:  " << (double(locks)/marks)
             << "\nwrites ratio: " << (double(writes)/locks);

    unlink (fname);
}
END_TEST

#define WAIT_FOR(cond)                                                  \
    { int count = 1000; while (--count && !(cond)) { usleep (TEST_USLEEP); }}

Suite* saved_state_suite()
{
    Suite* s = suite_create ("saved_state");
    TCase* tc;

    tc = tcase_create ("saved_state");
    tcase_add_test  (tc, test_basic);
    tcase_add_test  (tc, test_unsafe);
    tcase_add_test  (tc, test_corrupt);
    tcase_set_timeout(tc, 120);
    suite_add_tcase (s, tc);

    return s;
}
