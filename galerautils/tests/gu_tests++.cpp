// Copyright (C) 2009 Codership Oy <info@codership.com>

#include <cstdlib>
#include <cstdio>
#include <string>

extern "C" {
#include "../src/gu_conf.h"
}

#include "gu_tests++.hpp"

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

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
