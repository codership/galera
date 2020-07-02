//
// Copyright (C) 2015 Codership Oy <info@codership.com>
//

#include "gu_shared_ptr.hpp"
#include "gu_logger.hpp"
#include "gu_datetime.hpp"

#include "gu_shared_ptr_test.hpp"

typedef gu::shared_ptr<long>::type LongPtr;

static void
__attribute__((noinline))
pass_by_value(LongPtr val, long& acc)
{
    acc += *val;
}

static void
__attribute__((noinline))
pass_by_const_ref(const LongPtr& val, long& acc)
{
    acc += *val;
}

static LongPtr
__attribute__((noinline))
construct_and_ret(long i)
{
    return LongPtr(new long(i));
}


static double to_sec(const gu::datetime::Period& p)
{
    return double(p.get_nsecs())/gu::datetime::Sec;
}

START_TEST(shared_ptr_test)
{

    long acc(0);
    long iters(10000);
    gu::datetime::Date start(gu::datetime::Date::monotonic());

    LongPtr longptr(new long(0));
    for (long i(0); i < iters; ++i)
    {
        *longptr = i;
        pass_by_value(longptr, acc);
    }

    gu::datetime::Date end(gu::datetime::Date::monotonic());

    log_info << "add_by_val: " << acc << " " << iters/to_sec(end - start);

    start = gu::datetime::Date::monotonic();

    for (long i(0); i < iters; ++i)
    {
        *longptr = i;
        pass_by_const_ref(longptr, acc);
    }

    end = gu::datetime::Date::monotonic();

    log_info << "add_by_const_ref: " << acc << " " << iters/to_sec(end - start);


    start = gu::datetime::Date::monotonic();

    for (long i(0); i < iters; ++i)
    {
        LongPtr longptr(construct_and_ret(i));
        acc += *longptr;
    }

    end = gu::datetime::Date::monotonic();

    log_info << "construct_and_ret: " << acc << " "
             << iters/to_sec(end - start);


}
END_TEST


Suite* gu_shared_ptr_suite(void)
{
    Suite* s(suite_create("galerautils++ shared_ptr"));
    TCase* tc(tcase_create("shared_ptr"));
    suite_add_tcase(s, tc);
    tcase_add_test(tc, shared_ptr_test);
    return s;
}


