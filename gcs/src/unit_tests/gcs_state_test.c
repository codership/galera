// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include <string.h>
#include "gcs_state_test.h"
#define GCS_STATE_ACCESS
#include "../gcs_state.h"

START_TEST (gcs_state_test)
{
    ssize_t send_len, ret;
    gcs_state_t* send_state;
    gcs_state_t* recv_state;

    send_state = gcs_state_create (1234, 3465, 457, false, true,
                                     "My Name", "192.168.0.1:2345", 0, 1);
    fail_if (NULL == send_state);

    send_len = gcs_state_msg_len (send_state);
    fail_if (send_len < 0, "gcs_state_msg_len() returned %zd (%s)",
             send_len, strerror (-send_len));
    {
        uint8_t send_buf[send_len];

        ret = gcs_state_msg_write (send_buf, send_state);
        fail_if (ret != send_len, "Return value does not match send_len: "
                 "expected %zd, got %zd", send_len, ret);

        recv_state = gcs_state_msg_read (send_buf, send_len);
        fail_if (NULL == recv_state);
    }

    fail_if (send_state->proto_min != recv_state->proto_min);
    fail_if (send_state->proto_max != recv_state->proto_max);
    fail_if (send_state->act_id    != recv_state->act_id);
    fail_if (send_state->comp_id   != recv_state->comp_id);
    fail_if (send_state->conf_id   != recv_state->conf_id);
    fail_if (send_state->joined    != recv_state->joined);
    fail_if (strcmp(send_state->name,     recv_state->name));
    fail_if (strcmp(send_state->inc_addr, recv_state->inc_addr));

    {
        size_t str_len = 1024;
        char   send_str[str_len];
        char   recv_str[str_len];

        fail_if (gcs_state_snprintf (send_str, str_len, send_state) <= 0);
        fail_if (gcs_state_snprintf (recv_str, str_len, recv_state) <= 0);
        fail_if (strncmp (send_str, recv_str, str_len));
    }

    gcs_state_destroy (send_state);
    gcs_state_destroy (recv_state);
}
END_TEST

Suite *gcs_state_suite(void)
{
  Suite *s  = suite_create("GCS state message");
  TCase *tc = tcase_create("gcs_state");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gcs_state_test);
  return s;
}

