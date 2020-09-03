//
// Copyright (C) 2016-2020 Codership Oy <info@codership.com>
//

#include "gu_thread.hpp"
#include <sstream>

#include "gu_thread_test.hpp"

START_TEST(check_thread_schedparam_parse)
{
    gu::ThreadSchedparam sp_other(SCHED_OTHER, 0);

    std::ostringstream oss;
    oss << sp_other;
    ck_assert_msg(oss.str() == "other:0", "'%s'", oss.str().c_str());

    oss.str("");

    gu::ThreadSchedparam sp_fifo(SCHED_FIFO, 95);
    oss << sp_fifo;
    ck_assert_msg(oss.str() == "fifo:95", "'%s'", oss.str().c_str());

    oss.str("");

    gu::ThreadSchedparam sp_rr(SCHED_RR, 96);
    oss << sp_rr;
    ck_assert_msg(oss.str() == "rr:96", "'%s'", oss.str().c_str());


}
END_TEST

START_TEST(check_thread_schedparam_system_default)
{

    gu::ThreadSchedparam sp(gu::thread_get_schedparam(gu_thread_self()));
    std::ostringstream sp_oss;
    sp_oss << sp;

    std::ostringstream system_default_oss;
    system_default_oss << gu::ThreadSchedparam::system_default;

    ck_assert_msg(sp == gu::ThreadSchedparam::system_default,
                  "sp '%s' != system default '%s'",
                  sp_oss.str().c_str(),
                  system_default_oss.str().c_str());
}
END_TEST

Suite* gu_thread_suite()
{
    Suite* s(suite_create("galerautils Thread"));
    TCase* tc(tcase_create("schedparam"));

    suite_add_tcase(s, tc);
    tcase_add_test(tc, check_thread_schedparam_parse);
    tcase_add_test(tc, check_thread_schedparam_system_default);

    return s;
}
