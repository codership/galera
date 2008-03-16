// Copyright (C) 2007 Codership Oy <info@codership.com>
#include <check.h>
#include <string.h>

#include "../src/wsdb_priv.h"
#include "wsdb_test.h"

static char log_str[1024];

//static - compiler does not like it
void galera_log_cb(int rcode, const char *msg) {
    //fprintf(stdout, "code: %d, msg: %s", rcode, msg);
    if (!strstr(msg, log_str)) {
        fail("log fail: >%s< != >%s<", msg, log_str);
    }
}

START_TEST (test_log)
{
    gu_conf_set_log_callback(galera_log_cb);

    memset(log_str, '\0', 1024);
    strcpy(log_str, "FATAL MESSAGE"); 
    gu_fatal("FATAL MESSAGE");

    memset(log_str, '\0', 1024);
    strcpy(log_str, "one arg: ERROR MESSAGE"); 
    gu_error("one arg: %s", "ERROR MESSAGE");

    memset(log_str, '\0', 1024);
    strcpy(log_str, "two args: 123 - Warning"); 
    gu_warn("two args: %d - %s", 123, "Warning");

    memset(log_str, '\0', 1024);
    strcpy(log_str, "three args: 99999, INFO:, "); 
    gu_info("three args: %d, %s, %p", 99999, "INFO:", galera_log_cb);

}
END_TEST

Suite *make_log_suite(void)
{
  Suite *s = suite_create("log");
  TCase *tc_core = tcase_create("Core");

  suite_add_tcase (s, tc_core);
  tcase_add_test(tc_core, test_log);
  
  return s;
}
