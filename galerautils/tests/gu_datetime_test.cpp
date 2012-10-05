/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_datetime.hpp"
#include "gu_logger.hpp"
#include "gu_utils.hpp"

#include "gu_datetime_test.hpp"



using namespace gu;
using namespace gu::datetime;

START_TEST(test_units)
{
    fail_unless(NSec  ==                                           1LL);
    fail_unless(USec  ==                                        1000LL);
    fail_unless(MSec  ==                                 1000LL*1000LL);
    fail_unless(Sec   ==                          1000LL*1000LL*1000LL);
    fail_unless(Min   ==                     60LL*1000LL*1000LL*1000LL);
    fail_unless(Hour  ==                60LL*60LL*1000LL*1000LL*1000LL);
    fail_unless(Day   ==           24LL*60LL*60LL*1000LL*1000LL*1000LL);
    fail_unless(Month ==      30LL*24LL*60LL*60LL*1000LL*1000LL*1000LL);
    fail_unless(Year  == 12LL*30LL*24LL*60LL*60LL*1000LL*1000LL*1000LL);


}
END_TEST

START_TEST(test_period)
{
    // Zero periods
    fail_unless(Period("").get_nsecs() == 0);
    fail_unless(Period("P").get_nsecs() == 0);
    fail_unless(Period("PT").get_nsecs() == 0);

    // Year-mon-day
    fail_unless(Period("P3Y").get_nsecs() == 3*Year);
    fail_unless(Period("P5M").get_nsecs() == 5*Month);
    fail_unless(Period("P37D").get_nsecs() == 37*Day);

    fail_unless(Period("P3Y17M").get_nsecs() == 3*Year + 17*Month);
    fail_unless(Period("P5Y66D").get_nsecs() == 5*Year + 66*Day);
    fail_unless(Period("P37M44D").get_nsecs() == 37*Month + 44*Day);
    
    fail_unless(Period("P3YT").get_nsecs() == 3*Year);
    fail_unless(Period("P5MT").get_nsecs() == 5*Month);
    fail_unless(Period("P37DT").get_nsecs() == 37*Day);

    fail_unless(Period("P3Y17MT").get_nsecs() == 3*Year + 17*Month);
    fail_unless(Period("P5Y66DT").get_nsecs() == 5*Year + 66*Day);
    fail_unless(Period("P37M44DT").get_nsecs() == 37*Month + 44*Day);


    // Hour-min-sec
    fail_unless(Period("PT3H").get_nsecs() == 3*Hour);
    fail_unless(Period("PT5M").get_nsecs() == 5*Min);
    fail_unless(Period("P37S").get_nsecs() == 37*Sec);

    // fail_unless(Period("PT3.578777S").get_nsecs() == 3*Sec + 578*MSec + 777*USec);
    fail_unless(Period("PT0.5S").get_nsecs() == 500*MSec);


    // fail_unless(Period("PT5H7M3.578777S").get_nsecs() == 5*Hour + 7*Min + 3*Sec + 578*MSec + 777*USec);    
    
    // @todo these should fail
    fail_unless(Period("PT.S").get_nsecs() == 0);
    fail_unless(Period("PT.D").get_nsecs() == 0);

}
END_TEST

START_TEST(test_date)
{
    Date d1(Date::now());
    Date d2 = d1 + Period("PT6S");
    fail_unless(d2.get_utc() == d1.get_utc() + 6*Sec);
    fail_unless(d2 - Period("PT6S") == d1);

    Date max(Date::max());
    fail_unless(d1 < max);

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

    tc = tcase_create("test_date");
    tcase_add_test(tc, test_date);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_trac_712");
    tcase_add_test(tc, test_trac_712);
    suite_add_tcase(s, tc);

    return s;
}
