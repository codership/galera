/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "../src/galera_service_thd.hpp"
#include <check.h>
#include <errno.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

using namespace galera;


START_TEST(service_thd1)
{
    DummyGcs conn;
    ServiceThd* thd = new ServiceThd(conn);
    fail_if (thd == 0);
    delete thd;
}
END_TEST

#define TEST_USLEEP 1000 // 1ms
#define WAIT_FOR(cond)                                                  \
    { int count = 1000; while (--count && !(cond)) { usleep (TEST_USLEEP); }}

START_TEST(service_thd2)
{
    DummyGcs conn;
    ServiceThd* thd = new ServiceThd(conn);
    fail_if (thd == 0);

    conn.set_last_applied(0);

    gcs_seqno_t seqno = 1;
    thd->report_last_committed (seqno);
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %"PRId64", expected %"PRId64, conn.last_applied(), seqno);

    seqno = 5;
    thd->report_last_committed (seqno);
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %"PRId64", expected %"PRId64, conn.last_applied(), seqno);

    thd->report_last_committed (3);
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %"PRId64", expected %"PRId64, conn.last_applied(), seqno);

    thd->reset();

    seqno = 3;
    thd->report_last_committed (seqno);
    WAIT_FOR(conn.last_applied() == seqno);
    fail_if (conn.last_applied() != seqno,
             "seqno = %"PRId64", expected %"PRId64, conn.last_applied(), seqno);

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
    suite_add_tcase (s, tc);

    return s;
}
