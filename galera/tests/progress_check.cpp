/*
 * Copyright (C) 2021 Codership Oy <info@codership.com>
 */

#include "../src/progress_callback.hpp"

#include <check.h>
#include <errno.h>

#include <string>
#include <vector>
#include <chrono>
#include <thread> // to sleep in C++ style

struct wsrep_event_context
{
    std::vector<std::string> exp;
    wsrep_event_context()
        : exp()
    {
        exp.push_back("{ \"from\": 3, \"to\": 4, \"total\": 2, \"done\": 2, \"undefined\": -1 }");
        exp.push_back("{ \"from\": 3, \"to\": 4, \"total\": 2, \"done\": 1, \"undefined\": -1 }");
        exp.push_back("{ \"from\": 3, \"to\": 4, \"total\": 2, \"done\": 0, \"undefined\": -1 }");
    }
};

static void
event_cb(wsrep_event_context_t* ctx, const char* name, const char* value)
{
    ck_assert(strcmp(name, "progress") == 0); // ensure proper event name

    const std::string& exp(ctx->exp.back());
    ck_assert_msg(strcmp(exp.c_str(), value) == 0,
                  "Expected '%s', got '%s'",  exp.c_str(), value);

    ctx->exp.pop_back();
}

START_TEST(progress_callback)
{
    wsrep_event_context_t event_context;
    wsrep_event_service_v1_t evs = { event_cb, &event_context };

    galera::EventService::init_v1(&evs);

    {
        galera::ProgressCallback<int> pcb(WSREP_MEMBER_JOINED,
                                          WSREP_MEMBER_SYNCED);

        /* Ctor calls event callback for the first time */
        gu::Progress<int> prog(&pcb, "Testing", " units", 2, 1);

        /* This calls event callback for the second time. Need to sleep
         * a second here due to certain rate limiting in progress object */
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        prog.update(1);
        prog.finish();

        /* Dtor calls event callback for the third time */
    }

    galera::EventService::deinit_v1();
}
END_TEST

Suite* progress_suite()
{
    Suite* s = suite_create ("progress_suite");
    TCase* tc;

    tc = tcase_create ("progress_case");
    tcase_add_test  (tc, progress_callback);
    tcase_set_timeout(tc, 60);
    suite_add_tcase (s, tc);

    return s;
}
