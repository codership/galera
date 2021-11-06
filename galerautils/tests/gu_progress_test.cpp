/*
 * Copyright (C) 2021 Codership Oy <info@codership.com>
 */

#include "../src/gu_progress.hpp"

#include <check.h>
#include <errno.h>

#include <utility> // std::pair
#include <vector>
#include <chrono>
#include <thread> // to sleep in C++ style

class callback : public gu::Progress<int>::Callback
{
    std::vector<std::pair<int, int> > expect_;

public:
    callback()
        : expect_()
    {
        expect_.push_back(std::pair<int, int>(3,3));
        expect_.push_back(std::pair<int, int>(2,3));
        expect_.push_back(std::pair<int, int>(1,2));
        expect_.push_back(std::pair<int, int>(0,2));
    }

    void operator()(int const first, int const second)
    {
        std::pair<int, int> const exp(expect_.back());
        bool const equal(exp == std::pair<int, int>(first, second));
        ck_assert_msg(equal, "Expected (%d, %d), got (%d, %d)",
                      exp.first, exp.second, first, second);
        expect_.pop_back();
    }
};

START_TEST(progress)
{
    callback cb;

    {
        /* Ctor calls event callback for the first time */
        gu::Progress<int> prog(&cb, "Testing", " units", 2, 1);

        /* This calls event callback for the second time. Need to sleep
         * a second here due to certain rate limiting in progress object */
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        prog.update(1);

        /* THis extends the amount of "work" by 1
         * (to test "crawling" progress of catching up, for example) */
        prog.update_total(1);

        /* THis calls event callback for the 3rd time */
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        prog.update(1);

        prog.finish();

        /* Progress dtor calls event callback for the 4th time */
    }
}
END_TEST

Suite* progress_suite()
{
    Suite* s = suite_create ("progress_suite");
    TCase* tc;

    tc = tcase_create ("progress_case");
    tcase_add_test  (tc, progress);
    tcase_set_timeout(tc, 60);
    suite_add_tcase (s, tc);

    return s;
}
