// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include <string.h>
#include "gcs_state_test.h"
#define GCS_STATE_ACCESS
#include "../gcs_state.h"

START_TEST (gcs_state_test_basic)
{
    ssize_t send_len, ret;
    gu_uuid_t    state_uuid;
    gu_uuid_t    group_uuid;
    gcs_state_t* send_state;
    gcs_state_t* recv_state;

    gu_uuid_generate (&state_uuid, NULL, 0);
    gu_uuid_generate (&group_uuid, NULL, 0);

    send_state = gcs_state_create (&state_uuid, &group_uuid, 3465, 457,
                                   GCS_STATE_JOINED,
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
    fail_if (send_state->conf_id   != recv_state->conf_id);
    fail_if (send_state->status    != recv_state->status);
    fail_if (gu_uuid_compare (&recv_state->state_uuid, &state_uuid));
    fail_if (gu_uuid_compare (&recv_state->group_uuid, &group_uuid));
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

START_TEST (gcs_state_test_quorum)
{
    gcs_state_t* st1, *st2, *st3;
    gu_uuid_t    g1, g2, g3;

    gu_uuid_generate (&g1, NULL, 0);
    gu_uuid_generate (&g2, NULL, 0);
    gu_uuid_generate (&g3, NULL, 0);
    
    st1 = st2 = st3 = NULL;
    
    mark_point();
}
END_TEST

Suite *gcs_state_suite(void)
{
  Suite *s  = suite_create("GCS state message");
  TCase *tc = tcase_create("gcs_state");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gcs_state_test_basic);
  tcase_add_test  (tc, gcs_state_test_quorum);
  return s;
}

