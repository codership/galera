/*
 * Copyright (C) 2008-2017 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdio.h>  // printf()
#include <string.h> // strcmp()
#include <stdlib.h> // EXIT_SUCCESS | EXIT_FAILURE
#include <check.h>

#include <galerautils.h>
#include "gcs_comp_test.hpp"
#include "gcs_sm_test.hpp"
#include "gcs_state_msg_test.hpp"
#include "gcs_fifo_test.hpp"
#include "gcs_proto_test.hpp"
#include "gcs_defrag_test.hpp"
#include "gcs_node_test.hpp"
#include "gcs_memb_test.hpp"
#include "gcs_act_cchange_test.hpp"
#include "gcs_group_test.hpp"
#include "gcs_backend_test.hpp"
#include "gcs_core_test.hpp"
#include "gcs_fc_test.hpp"

typedef Suite *(*suite_creator_t)(void);

static suite_creator_t suites[] =
    {
	gcs_comp_suite,
	gcs_send_monitor_suite,
	gcs_state_msg_suite,
	gcs_fifo_suite,
	gcs_proto_suite,
	gcs_defrag_suite,
	gcs_node_suite,
	gcs_memb_suite,
	gcs_act_cchange_suite,
	gcs_group_suite,
	gcs_backend_suite,
	gcs_core_suite,
	gcs_fc_suite,
	NULL
    };

#define LOG_FILE "gcs_tests.log"

int main(int argc, char* argv[])
{
  bool const nofork(((argc > 1) && !strcmp(argv[1], "nofork")) ? true : false);
  int i       = 0;
  int failed  = 0;

  FILE* log_file = NULL;

  if (!nofork)
  {
    log_file = fopen (LOG_FILE, "w");
    if (!log_file) return EXIT_FAILURE;
    gu_conf_set_log_file (log_file);
  }
  gu_conf_debug_on();
  gu_conf_self_tstamp_on();

  while (suites[i]) {
      SRunner* sr = srunner_create(suites[i]());

      gu_info ("#########################");
      gu_info ("Test %d.", i);
      gu_info ("#########################");
      if (nofork) srunner_set_fork_status(sr, CK_NOFORK);
      srunner_run_all (sr, CK_NORMAL);
      failed += srunner_ntests_failed (sr);
      srunner_free (sr);
      i++;
  }

  if (log_file) fclose (log_file);
  printf ("Total test failed: %d\n", failed);

  if (0 == failed && 0 != log_file) ::unlink(LOG_FILE);

  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* When the suite compiled in debug mode, returns number of allocated bytes */
ssize_t
gcs_tests_get_allocated()
{
    ssize_t total;
    ssize_t allocs;
    ssize_t reallocs;
    ssize_t deallocs;

    void gu_mem_stats (ssize_t*, ssize_t*, ssize_t*, ssize_t*);
    gu_mem_stats (&total, &allocs, &reallocs, &deallocs);

    return total;
}
