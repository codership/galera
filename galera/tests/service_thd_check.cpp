/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "../src/galera_service_thd.hpp"
#include <check.h>
#include <errno.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

using namespace galera;

extern "C" {
    struct gcs_conn
    {
        volatile gcs_seqno_t seqno;
    };
}

static gcs_conn_t test_conn;

static const gcs_seqno_t test_limit = 10;

long
gcs_set_last_applied (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    long ret = 0;

    if (seqno < test_limit)
    {
        conn->seqno = seqno;
    }
    else
    {
        conn->seqno = test_limit - seqno; // just to verify that there was error
        ret = -ERANGE;
    }

    return ret;
}

START_TEST(service_thd1)
{
    ServiceThd* thd = new ServiceThd(&test_conn);
    fail_if (thd == 0);
    delete thd;
}
END_TEST

#define TEST_USLEEP 10000 // 10ms

START_TEST(service_thd2)
{
    ServiceThd* thd = new ServiceThd(&test_conn);
    fail_if (thd == 0);

    test_conn.seqno = 0;

    gcs_seqno_t seqno = 1;
    thd->report_last_committed (seqno);
    usleep (TEST_USLEEP);
    fail_if (test_conn.seqno != seqno, "seqno = %"PRId64", expected %"PRId64,
             test_conn.seqno, seqno);

    seqno = 5;
    thd->report_last_committed (seqno);
    usleep (TEST_USLEEP);
    fail_if (test_conn.seqno != seqno, "seqno = %"PRId64", expected %"PRId64,
             test_conn.seqno, seqno);

    thd->report_last_committed (3);
    usleep (TEST_USLEEP);
    fail_if (test_conn.seqno != seqno, "seqno = %"PRId64", expected %"PRId64,
             test_conn.seqno, seqno);

    thd->reset();

    seqno = 3;
    thd->report_last_committed (seqno);
    usleep (TEST_USLEEP);
    fail_if (test_conn.seqno != seqno, "seqno = %"PRId64", expected %"PRId64,
             test_conn.seqno, seqno);

    thd->report_last_committed (test_limit + seqno);
    usleep (TEST_USLEEP);
    fail_if (test_conn.seqno != -seqno, "seqno = %"PRId64", expected %"PRId64,
             test_conn.seqno, -seqno);

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
