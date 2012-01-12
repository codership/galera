// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <stdio.h>  // printf()
#include <string.h> // strcmp()
#include <stdlib.h> // EXIT_SUCCESS | EXIT_FAILURE
#include <check.h>

#include "../src/gu_conf.h"
#include "gu_mem_test.h"
#include "gu_bswap_test.h"
#include "gu_fnv_test.h"
#include "gu_dbug_test.h"
#include "gu_time_test.h"
#include "gu_fifo_test.h"
#include "gu_uuid_test.h"
#include "gu_lock_step_test.h"
#include "gu_str_test.h"
#include "gu_utils_test.h"

typedef Suite *(*suite_creator_t)(void);

static suite_creator_t suites[] =
    {
        gu_mem_suite,
        gu_bswap_suite,
        gu_fnv_suite,
        gu_dbug_suite,
        gu_time_suite,
        gu_fifo_suite,
        gu_uuid_suite,
        gu_lock_step_suite,
        gu_str_suite,
        gu_utils_suite,
        NULL
    };

int main(int argc, char* argv[])
{
  int no_fork = ((argc > 1) && !strcmp(argv[1], "nofork")) ? 1 : 0;
  int i       = 0;
  int failed  = 0;

  FILE* log_file = NULL;
  
  if (!no_fork) {
      log_file = fopen ("gu_tests.log", "w");
      if (!log_file) return EXIT_FAILURE;
      gu_conf_set_log_file (log_file);
  }
  gu_conf_debug_on();

  while (suites[i]) {
      SRunner* sr = srunner_create(suites[i]());

      if (no_fork) srunner_set_fork_status(sr, CK_NOFORK);
      srunner_run_all (sr, CK_NORMAL);
      failed += srunner_ntests_failed (sr);
      srunner_free (sr);
      i++;
  }
  if (log_file)
  {
      fclose (log_file);
  }
  printf ("Total tests failed: %d\n", failed);
  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
