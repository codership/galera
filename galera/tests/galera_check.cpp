/*
 * Copyright (C) 2012-2018 Codership Oy <info@codership.com>
 */

#include <cstdlib>
#include <cstdio>
#include <string>
#include <check.h>

/*
 * Suite descriptions: forward-declare and add to array
 */
typedef Suite* (*suite_creator_t) (void);

extern Suite* data_set_suite();
extern Suite* key_set_suite();
extern Suite* write_set_ng_suite();
extern Suite* certification_suite();
//extern Suite* write_set_suite();
extern Suite* trx_handle_suite();
extern Suite* service_thd_suite();
extern Suite* ist_suite();
extern Suite* saved_state_suite();
extern Suite* defaults_suite();

static suite_creator_t suites[] =
{
    data_set_suite,
    key_set_suite,
    write_set_ng_suite,
    certification_suite,
    trx_handle_suite,
    service_thd_suite,
    ist_suite,
    saved_state_suite,
    defaults_suite,
    0
};

extern "C" {
#include <galerautils.h>
}

#define LOG_FILE "galera_check.log"

int main(int argc, char* argv[])
{
    bool  no_fork  = (argc >= 2 && std::string(argv[1]) == "nofork");
    FILE* log_file = 0;

    if (!no_fork)
    {
        log_file = fopen (LOG_FILE, "w");
        if (!log_file) return EXIT_FAILURE;
        gu_conf_set_log_file (log_file);
    }

    gu_conf_debug_on();

    int failed = 0;

    for (int i = 0; suites[i] != 0; ++i)
    {
        SRunner* sr = srunner_create(suites[i]());

        if (no_fork) srunner_set_fork_status(sr, CK_NOFORK);

        srunner_run_all(sr, CK_NORMAL);
        failed += srunner_ntests_failed(sr);
        srunner_free(sr);
    }

    if (log_file != 0) fclose(log_file);
    printf ("Total tests failed: %d\n", failed);

    if (0 == failed && 0 != log_file) ::unlink(LOG_FILE);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
