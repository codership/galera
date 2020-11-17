/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

/*
 * @note This test file does not use any unit test library
 *       framework in order to keep the link time dependencies
 *       minimal.
 */

#include "gu_conf.h"

#include "wsrep_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_FILE "wsrep_tests.log"
FILE* log_file = NULL;
static void log_fn(wsrep_log_level_t level, const char* msg)
{
    FILE* f = (log_file ? log_file : stdout);
    fprintf(f, "%d: %s", level, msg);
}

static const char* get_provider()
{
    return WSREP_PROVIDER;
}

#define FAIL_UNLESS(x) if (!(x)) abort()

int wsrep_load_unload()
{
    wsrep_t* wsrep = 0;
    FAIL_UNLESS(wsrep_load(get_provider(), &wsrep, &log_fn) == 0);
    FAIL_UNLESS(wsrep != NULL);
    wsrep_unload(wsrep);
    return 0;
}

int main(int argc, char* argv[])
{
    int no_fork = ((argc > 1) && !strcmp(argv[1], "nofork")) ? 1 : 0;
    int failed  = 0;


    if (!no_fork) {
        log_file = fopen (LOG_FILE, "w");
        if (!log_file) return EXIT_FAILURE;
    }

    failed += wsrep_load_unload();

    if (log_file)
    {
        fclose (log_file);
    }

    if (0 == failed && NULL != log_file) unlink(LOG_FILE);

    printf ("Total tests failed: %d\n", failed);
    return (failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
