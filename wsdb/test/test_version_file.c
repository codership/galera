// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include <string.h>

#include "../src/version_file.h"
#include "../src/wsdb_priv.h"
#include "../src/wsdb_file.h"
#include "wsdb_test.h"

START_TEST (test_vfile)
{
    int i = 0;
    /* unit test code */
    struct wsdb_file *file= version_file_open("./data", "test_vfile", 399, 10);
    if (!file) {
        fail("vfile create: %d", i);
    }
    for (i=0; i<100; i++) {
        char data[10];
        file_addr_t addr;
        memset(data, (char)('a' + i%20), 10);        
        addr = file->append_block(file, 10, data);
    }
    fail_if(file->close(file), "file close");
}
END_TEST

Suite *make_version_file_suite(void)
{
    Suite *s = suite_create("version_file");
    TCase *tc_core = tcase_create("Core");

    suite_add_tcase (s, tc_core);
    tcase_add_test(tc_core, test_vfile);
  
    return s;
}
