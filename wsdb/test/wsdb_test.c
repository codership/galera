// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include <stdio.h>
#include <string.h>

#include "wsdb_test.h"

typedef Suite *(*suite_creator_t)(void);
static suite_creator_t suites[] = {
	make_file_suite,
	make_hash_suite,
	make_version_file_suite,
	make_cache_suite,
	make_wsdb_suite,
	make_xdr_suite,
	make_log_suite,
	make_mempool_suite,
	NULL
};
int main(int argc, char **argv) {
    int nf = 0;
    Suite *s;
    char c;
    int i = 0;
    int no_fork = 
        ((argc > 1) && !strcmp(argv[1], "nofork")) ? 1 : 0;

    while(suites[i]) {
        fprintf(stdout, "Testing :%d\n", i);
        fflush(stdout);

        s = suites[i]();
        SRunner *sr = srunner_create(s);
        
        if (no_fork) srunner_set_fork_status(sr, CK_NOFORK);
        
        srunner_run_all(sr, CK_VERBOSE);
        fprintf(stdout, "Tested :%d\n", i);
        fflush(stdout);
        nf = srunner_ntests_failed(sr);
        srunner_free(sr);
        fprintf(stdout, "Complete :%d\n", i);
        fflush(stdout);
        i++;
    }
  
    fprintf(stdout, "Press a key:");
    fflush(stdout);
    c = fgetc(stdin);
    fprintf(stdout, "Done\n");
    fflush(stdout);
    return (nf == 0) ? 0 : -1;
}
