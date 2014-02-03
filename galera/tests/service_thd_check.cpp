/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */
#define __STDC_FORMAT_MACROS

#include "../src/galera_service_thd.hpp"
#include "../src/replicator_smm.hpp"
#include <check.h>
#include <errno.h>

#include <inttypes.h>

namespace
{
    class TestEnv
    {
    public:

        TestEnv() :
            conf_   (),
            init_   (conf_, NULL),
            gcache_ (conf_, "."),
            gcs_    (conf_, gcache_)
        {}

        gcache::GCache&   gcache()  { return gcache_; }
        galera::DummyGcs& gcs()     { return gcs_;    }

    private:

        gu::Config       conf_;
        galera::ReplicatorSMM::InitConfig init_;
        gcache::GCache   gcache_;
        galera::DummyGcs gcs_;
    };
}

using namespace galera;

START_TEST(service_thd1)
{
    TestEnv env;
    ServiceThd* thd = new ServiceThd(env.gcs(), env.gcache());
    fail_if (thd == 0);
    delete thd;
}
END_TEST

#define TEST_USLEEP 1000 // 1ms
#define WAIT_FOR(cond)                                                  \
    { int count = 1000; while (--count && !(cond)) { usleep (TEST_USLEEP); }}

START_TEST(service_thd2)
{
    TestEnv env;
    DummyGcs& conn(env.gcs());
    ServiceThd* thd = new ServiceThd(conn, env.gcache());
    fail_if (thd == 0);

    conn.set_last_applied(0);

    gcs_seqno_t seqno = 1;
    thd->report_last_committed (seqno);
    thd->flush();
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %" PRId64 ", expected %" PRId64, conn.last_applied(),
             seqno);

    seqno = 5;
    thd->report_last_committed (seqno);
    thd->flush();
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %" PRId64 ", expected %" PRId64, conn.last_applied(),
             seqno);

    thd->report_last_committed (3);
    thd->flush();
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %" PRId64 ", expected %" PRId64, conn.last_applied(),
             seqno);

    thd->reset();

    seqno = 3;
    thd->report_last_committed (seqno);
    thd->flush();
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %" PRId64 ", expected %" PRId64, conn.last_applied(),
             seqno);

    delete thd;
}
END_TEST

START_TEST(service_thd3)
{
    TestEnv env;
    ServiceThd* thd = new ServiceThd(env.gcs(), env.gcache());
    fail_if (thd == 0);
    // so far for empty GCache the following should be a noop.
    thd->release_seqno(-1);
    thd->release_seqno(2345);
    thd->release_seqno(234645676);
    delete thd;
}
END_TEST

Suite* service_thd_suite()
{
    Suite* s = suite_create ("service_thd");
    TCase* tc;

    tc = tcase_create ("service_thd");
    tcase_add_test  (tc, service_thd1);
    tcase_add_test  (tc, service_thd2);
    tcase_add_test  (tc, service_thd3);
    suite_add_tcase (s, tc);

    return s;
}
