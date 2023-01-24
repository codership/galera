/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "gu_datetime.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"
#include "gu_datetime_test.hpp"

#include <cmath> // std::fabs

using namespace gu;
using namespace gu::datetime;

START_TEST(test_units)
{
    ck_assert(NSec  ==                                           1LL);
    ck_assert(USec  ==                                        1000LL);
    ck_assert(MSec  ==                                 1000LL*1000LL);
    ck_assert(Sec   ==                          1000LL*1000LL*1000LL);
    ck_assert(Min   ==                     60LL*1000LL*1000LL*1000LL);
    ck_assert(Hour  ==                60LL*60LL*1000LL*1000LL*1000LL);
    ck_assert(Day   ==           24LL*60LL*60LL*1000LL*1000LL*1000LL);
    ck_assert(Month ==      30LL*24LL*60LL*60LL*1000LL*1000LL*1000LL);
    ck_assert(Year  == 12LL*30LL*24LL*60LL*60LL*1000LL*1000LL*1000LL);


}
END_TEST

START_TEST(test_period)
{
    // Zero periods
    ck_assert(Period("").get_nsecs() == 0);
    ck_assert(Period("P").get_nsecs() == 0);
    ck_assert(Period("PT").get_nsecs() == 0);

    // Year-mon-day
    ck_assert(Period("P3Y").get_nsecs() == 3*Year);
    ck_assert(Period("P5M").get_nsecs() == 5*Month);
    ck_assert(Period("P37D").get_nsecs() == 37*Day);

    ck_assert(Period("P3Y17M").get_nsecs() == 3*Year + 17*Month);
    ck_assert(Period("P5Y66D").get_nsecs() == 5*Year + 66*Day);
    ck_assert(Period("P37M44D").get_nsecs() == 37*Month + 44*Day);
    
    ck_assert(Period("P3YT").get_nsecs() == 3*Year);
    ck_assert(Period("P5MT").get_nsecs() == 5*Month);
    ck_assert(Period("P37DT").get_nsecs() == 37*Day);

    ck_assert(Period("P3Y17MT").get_nsecs() == 3*Year + 17*Month);
    ck_assert(Period("P5Y66DT").get_nsecs() == 5*Year + 66*Day);
    ck_assert(Period("P37M44DT").get_nsecs() == 37*Month + 44*Day);

    // Hour-min-sec
    ck_assert(Period("PT3H").get_nsecs() == 3*Hour);
    ck_assert(Period("PT5M").get_nsecs() == 5*Min);
    ck_assert(Period("P37S").get_nsecs() == 37*Sec);

    // Decimal seconds
    ck_assert(Period("PT0.5S").get_nsecs() == 500*MSec);
    ck_assert(Period("PT3.578777S").get_nsecs() == 3*Sec + 578*MSec + 777*USec);
    ck_assert(Period("PT5H7M3.578777S").get_nsecs()
              == 5*Hour + 7*Min + 3*Sec + 578*MSec + 777*USec);

    // Full
    ck_assert(Period("P10Y5M4DT3H24M1.1S").get_nsecs()
              == 10*Year + 5*Month + 4*Day + 3*Hour + 24*Min + 1.1*Sec);
}
END_TEST

static void assert_invalid_period(const std::string& period)
{
    bool exception = false;
    try
    {
        Period p(period);
    }
    catch (gu::NotFound& exp)
    {
        exception = true;
    }
    ck_assert(exception);
}

START_TEST(test_period_invalid)
{
    assert_invalid_period("a");
    assert_invalid_period("anyvalue");
    assert_invalid_period("Panyvalue");
    assert_invalid_period("PT.S");
    assert_invalid_period("PT.D");
    assert_invalid_period("PT1D");
    assert_invalid_period("P1D1Y");
    assert_invalid_period("P9223372036854775807Y"); // Overflow
}
END_TEST

static void assert_double_eq_tol(double left, double right, double tol)
{
    ck_assert(std::fabs(left - right) < tol);
}

START_TEST(test_period_from_double)
{
    ck_assert(Period("0").get_nsecs() == 0);
    ck_assert(Period(".1").get_nsecs() ==  100*MSec);
    ck_assert(Period("0.0").get_nsecs() == 0);
    ck_assert(Period("0.5").get_nsecs() == 500*MSec);
    // Use microsecond precision for comparison to make
    // it work on x86
    assert_double_eq_tol(to_double(Period("0.5")), 0.5,  0.000001);
    assert_double_eq_tol(to_double(Period(".111111111")), 0.111111111,
                         0.000001);;
}
END_TEST

START_TEST(test_period_overflow)
{
    long long max_secs = std::numeric_limits<long long>::max() / gu::datetime::Sec;

    std::string max_duration("PT" + std::to_string(max_secs) + "S");
    ck_assert(Period(max_duration).get_nsecs()); // no overflow

    std::string overflow_duration("PT" + std::to_string(max_secs + 1.0) + "S");
    assert_invalid_period(overflow_duration);
}
END_TEST

START_TEST(test_date)
{
    Date d1(Date::monotonic());
    Date d2 = d1 + Period("PT6S");
    ck_assert(d2.get_utc() == d1.get_utc() + 6*Sec);
    ck_assert(d2 - Period("PT6S") == d1);

    Date max(Date::max());
    ck_assert(d1 < max);

}
END_TEST

START_TEST(test_trac_712)
{
    try
    {
        Period p;
        p = gu::from_string<Period>("0x3"); // used to throw gu::Exception
    }
    catch (gu::NotFound& nf)
    {

    }
}
END_TEST

Suite* gu_datetime_suite()
{
    Suite* s = suite_create("gu::datetime");
    TCase* tc;

    tc = tcase_create("test_units");
    tcase_add_test(tc, test_units);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_period");
    tcase_add_test(tc, test_period);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_period_invalid");
    tcase_add_test(tc, test_period_invalid);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_period_from_double");
    tcase_add_test(tc, test_period_from_double);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_period_overflow");
    tcase_add_test(tc, test_period_overflow);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_date");
    tcase_add_test(tc, test_date);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_712");
    tcase_add_test(tc, test_trac_712);
    suite_add_tcase(s, tc);

    return s;
}
