/*
 * Copyright (C) 2019 Codership Oy <info@codership.com>
 */

#include "check_gcomm.hpp"

#include "gu_string_utils.hpp" // strsplit()
#include "gu_exception.hpp"
#include "gu_logger.hpp"
#include "gu_crc32c.h" // gu_crc32c_configure()

#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <check.h>
#include <string.h>
#include <unistd.h>

// <using namespace gcomm;

using std::string;
using std::vector;

typedef Suite* (*suite_creator_f)();

struct GCommSuite
{
    string name;
    suite_creator_f suite;
};

static GCommSuite suites[] = {
    {"util_nondet", util_nondet_suite},
    {"gmcast_nondet", gmcast_suite},
    {"pc_nondet", pc_nondet_suite},
    {"", 0}
};

#define LOG_FILE "check_gcomm_nondet.log"

int main(int argc, char* argv[])
{
    SRunner* sr = srunner_create(0);
    vector<string>* suits = 0;
    FILE* log_file(0);

    if (argc > 1 && !strcmp(argv[1],"nofork")) {
        srunner_set_fork_status(sr, CK_NOFORK);
    }
    else if (argc > 1 && strcmp(argv[1], "nolog") == 0)
    { /* no log redirection */}
    else { // running in the background, loggin' to file
        log_file = fopen (LOG_FILE, "w");
        if (!log_file) return EXIT_FAILURE;
        gu_conf_set_log_file (log_file);

        // redirect occasional stderr there as well
        if (dup2(fileno(log_file), 2) < 0)
        {
            perror("dup2() failed: ");
            return EXIT_FAILURE;
        }
    }

    if (::getenv("CHECK_GCOMM_DEBUG"))
    {
        gu_log_max_level = GU_LOG_DEBUG;
        //gu::Logger::enable_debug(true);
    }

    gu_crc32c_configure();

    log_info << "check_gcomm_nondet, start tests";
    if (::getenv("CHECK_GCOMM_SUITES"))
    {
        suits = new vector<string>(gu::strsplit(::getenv("CHECK_GCOMM_SUITES"), ','));
    }

    for (size_t i = 0; suites[i].suite != 0; ++i)
    {
        if (suits == 0 ||
            find(suits->begin(), suits->end(), suites[i].name) != suits->end())
        {
            srunner_add_suite(sr, suites[i].suite());
        }
    }
    delete suits;
    suits = 0;

    srunner_run_all(sr, CK_NORMAL);
    log_info << "check_gcomm, run all tests";
    int n_fail = srunner_ntests_failed(sr);
    srunner_free(sr);

    if (0 == n_fail && 0 != log_file) ::unlink(LOG_FILE);

    return n_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
