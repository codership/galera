// Copyright (C) 2007 Codership Oy <info@codership.com>

#include <stdio.h>  // printf()
#include <string.h> // strcmp()
#include <stdlib.h> // EXIT_SUCCESS | EXIT_FAILURE
#include <check.h>
#include "gu_mem_test.h"
#include "gu_bswap_test.h"
#include "gu_dbug_test.h"

typedef Suite *(*suite_creator_t)(void);

static suite_creator_t suites[] =
    {
	gu_mem_suite,
	gu_bswap_suite,
	gu_dbug_suite,
	NULL
    };

int main(int argc, char* argv[])
{
  int no_fork = ((argc > 1) && !strcmp(argv[1], "nofork")) ? 1 : 0;
  int i       = 0;
  int failed  = 0;

  while (suites[i]) {
      SRunner* sr = srunner_create(suites[i]());

      if (no_fork) srunner_set_fork_status(sr, CK_NOFORK);
      srunner_run_all (sr, CK_NORMAL);
      failed += srunner_ntests_failed (sr);
      srunner_free (sr);
      i++;
  }

  printf ("Total test failed: %d\n", failed);
  return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
