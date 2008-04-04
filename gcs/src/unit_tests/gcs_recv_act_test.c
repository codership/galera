/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "gcs_recv_act_test.h"
#include "../gcs_recv_act.h"

#define TRUE (0 == 0)
#define FALSE (!TRUE)

// empty logger to prevent default output to stderr, Check closes it.
void logger (int s, const char* m) {};

static void
recv_act_check_init (gcs_recv_act_t* recv_act)
{
    fail_if (recv_act->sent_id  != GCS_SEQNO_ILL);
    fail_if (recv_act->head     != NULL);
    fail_if (recv_act->tail     != NULL);
    fail_if (recv_act->size     != 0);
    fail_if (recv_act->received != 0);
    fail_if (recv_act->frag_no  != 0);
    fail_if (recv_act->type     != 0);
}

START_TEST (gcs_recv_act_test)
{
    ssize_t ret;

    // The Action
    const char   act_buf[]   = "Test action smuction";
    size_t       act_len      = sizeof (act_buf);

    // lengths of three fragments of the action
    size_t       frag1_len    = act_len / 3;
    size_t       frag2_len    = frag1_len;
    size_t       frag3_len    = act_len - frag1_len - frag2_len;

    // pointer to the three fragments of the action
    const char*  frag1         = act_buf;
    const char*  frag2         = frag1 + frag1_len;
    const char*  frag3         = frag2 + frag2_len;

    // recv fragments
    gcs_act_frag_t frg1, frg2, frg3, frg4, frg5;

    gcs_recv_act_t recv_act;

    void* tail;
    char* act;

    gu_conf_set_log_callback (logger); // set empty logger

    mark_point();

    // Initialize message parameters
    frg1.act_id    = getpid();
    frg1.act_size  = act_len;
    frg1.frag      = frag1;
    frg1.frag_len  = frag1_len;
    frg1.frag_no   = 0;
    frg1.act_type  = GCS_ACT_DATA;
    frg1.proto_ver = 0;

    // normal fragments
    frg2 = frg3 = frg1;

    frg2.frag     = frag2;
    frg2.frag_len = frag2_len;
    frg2.frag_no  = frg1.frag_no + 1;
    frg3.frag     = frag3;
    frg3.frag_len = frag3_len;
    frg3.frag_no  = frg2.frag_no + 1;

    // bad fragmets to be tried instead of frg2
    frg4 = frg5 = frg2;
    frg4.frag     = "junk";
    frg4.frag_len = strlen("junk");
    frg4.act_id   = frg2.act_id + 1; // wrong action id
    frg5.frag     = "junk";
    frg5.frag_len = strlen("junk");
    frg5.act_type = GCS_ACT_SERVICE; // wrong action type

    mark_point();

    // ready for the first fragment
    gcs_recv_act_init (&recv_act);
    recv_act_check_init (&recv_act);

    mark_point();

    // 1. Try fragment that is not the first
    ret = gcs_recv_act_handle_frag (&recv_act, &frg3, FALSE);
    fail_if (ret != -EPROTO);
    mark_point();
    recv_act_check_init (&recv_act); // should be no changes

    // 2. Try first fragment
    ret = gcs_recv_act_handle_frag (&recv_act, &frg1, FALSE);
    fail_if (ret != 0);
    fail_if (recv_act.head == NULL);
    fail_if (recv_act.received != frag1_len);
    fail_if (recv_act.tail != recv_act.head + recv_act.received);
    tail = recv_act.tail;

#define TRY_WRONG_2ND_FRAGMENT(frag)                         \
    ret = gcs_recv_act_handle_frag (&recv_act, frag, FALSE);   \
    fail_if (ret != -EPROTO);                                \
    fail_if (recv_act.received != frag1_len);                \
    fail_if (recv_act.tail != tail);                         \

    // 3. Try first fragment again
    TRY_WRONG_2ND_FRAGMENT(&frg1);

    // 4. Try third fragment
    TRY_WRONG_2ND_FRAGMENT(&frg3);

    // 5. Try fouth fragment
    TRY_WRONG_2ND_FRAGMENT(&frg4);

    // 6. Try fifth fragment
    TRY_WRONG_2ND_FRAGMENT(&frg5);

    // 7. Try second fragment
    ret = gcs_recv_act_handle_frag (&recv_act, &frg2, FALSE);
    fail_if (ret != 0);
    fail_if (recv_act.received != frag1_len + frag2_len);
    fail_if (recv_act.tail != recv_act.head + recv_act.received);

    // 8. Try third fragment, last one
    ret = gcs_recv_act_handle_frag (&recv_act, &frg3, FALSE);
    fail_if (ret != act_len);
    fail_if (recv_act.received != act_len);

    // 9. Pop the action
    act = (char*) gcs_recv_act_pop (&recv_act);
    fail_if (act == NULL);
    fail_if (strncmp(act, act_buf, act_len),
             "Action received: '%s', expected '%s'", act, act_buf);
    recv_act_check_init (&recv_act); // should be empty

    // 10. Try the same with local action
    ret = gcs_recv_act_handle_frag (&recv_act, &frg1, TRUE);
    fail_if (ret != 0);
    fail_if (recv_act.head != NULL);
    ret = gcs_recv_act_handle_frag (&recv_act, &frg2, TRUE);
    fail_if (ret != 0);
    fail_if (recv_act.head != NULL);
    ret = gcs_recv_act_handle_frag (&recv_act, &frg3, TRUE);
    fail_if (ret != act_len);
    fail_if (recv_act.head != NULL);
    act = (char*) gcs_recv_act_pop (&recv_act);
    fail_if (act != NULL); // local action, must be fetched from local fifo  
}
END_TEST

Suite *gcs_recv_act_suite(void)
{
  Suite *suite = suite_create("GCS Receiving actions context");
  TCase *tcase = tcase_create("gcs_recv_act");

  suite_add_tcase (suite, tcase);
  tcase_add_test  (tcase, gcs_recv_act_test);
  return suite;
}

