/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <galerautils.h>

#define  GCS_COMP_MSG_ACCESS
#include "../gcs_comp_msg.hpp"
#include "gcs_comp_test.hpp"


static gcs_comp_memb_t const members[] =
{
    { "0",        0 },
    { "88888888", 1 },
    { "1",        5 },
    { "7777777",  1 },
    { "22",       3 },
    { "666666",   4 },
    { "333",      5 },
    { "55555",    5 },
    { "4444",     0 }
};

static char long_id[] =
"just make it longer when the test starts to fail because of increased limit";

static void
check_msg_identity (const gcs_comp_msg_t* m,
                    const gcs_comp_msg_t* n)
{
    long i;

    ck_assert(n->primary  == m->primary);
    ck_assert(n->my_idx   == m->my_idx);
    ck_assert(n->memb_num == m->memb_num);
    for (i = 0; i < m->memb_num; i++) {
        ck_assert_msg(strlen(n->memb[i].id) == strlen(m->memb[i].id),
                      "member %ld id len does not match: %zu vs %zu",
                      i, strlen(n->memb[i].id), strlen(m->memb[i].id));
        ck_assert_msg(!strncmp(n->memb[i].id, m->memb[i].id,
                               GCS_COMP_MEMB_ID_MAX_LEN),
                      "member %ld IDs don't not match: got '%s', "
                      "should be '%s'",
                      i, members[i].id, m->memb[i].id);
        ck_assert_msg(n->memb[i].segment == m->memb[i].segment,
                      "member %ld segments don't not match: got '%d', "
                      "should be '%d'",
                      i, (int)members[i].segment, (int)m->memb[i].segment);
    }
}

START_TEST (gcs_comp_test)
{
    long memb_num     = sizeof(members)/sizeof(members[0]);
    long my_idx       = getpid() % memb_num;
    long prim         = my_idx % 2;
    gcs_comp_msg_t* m = gcs_comp_msg_new (prim, false, my_idx, memb_num, 0);
    gcs_comp_msg_t* n = NULL;
    size_t buf_len    = gcs_comp_msg_size (m);
    char   buf[buf_len];
    long i, j;
    long ret;

    ck_assert(NULL != m);
    ck_assert(memb_num == gcs_comp_msg_num  (m));
    ck_assert(my_idx   == gcs_comp_msg_self (m));

    // add members except for the last
    for (i = 0; i < memb_num - 1; i++) {
        ret = gcs_comp_msg_add (m, members[i].id, members[i].segment);
        ck_assert_msg(ret == i, "gcs_comp_msg_add() returned %ld, expected %ld",
                      ret, i);
    }

    // try to add a id that was added already
    if (my_idx < i) {
        j = my_idx;
    } else {
        j = i - 1;
    }
    ret = gcs_comp_msg_add (m, members[j].id, members[j].segment);
    ck_assert_msg(ret == -ENOTUNIQ, "gcs_comp_msg_add() returned %ld, expected "
                  "-ENOTUNIQ (%d)", ret, -ENOTUNIQ);

    // try to add empty id
    ret = gcs_comp_msg_add (m, "", 0);
    ck_assert_msg(ret == -EINVAL, "gcs_comp_msg_add() returned %ld, expected "
                  "-EINVAL (%d)", ret, -EINVAL);

    // try to add id that is too long
    ret = gcs_comp_msg_add (m, long_id, 3);
    ck_assert_msg(ret == -ENAMETOOLONG,
                  "gcs_comp_msg_add() returned %ld, expected "
                  "-ENAMETOOLONG (%d)", ret, -ENAMETOOLONG);

    // add final id
    ret = gcs_comp_msg_add (m, members[i].id, members[i].segment);
    ck_assert_msg(ret == i, "gcs_comp_msg_add() returned %ld, expected %ld",
                  ret, i);

    // check that all added correctly
    for (i = 0; i < memb_num; i++) {
        const char* const id = gcs_comp_msg_member(m, i)->id;
        ck_assert_msg(!strcmp(members[i].id, id),
                      "Memeber %ld (%s) recorded as %s", i, members[i].id, id);
    }

    // check that memcpy preserves the message
    // (it can be treated just as a byte array)
    memcpy (buf, m, buf_len);
    n = (gcs_comp_msg_t*) buf;
    check_msg_identity (m, n);
    gcs_comp_msg_delete (m);

    mark_point();
    // check that gcs_comp_msg_copy() works
    m = gcs_comp_msg_copy (n);
    ck_assert(NULL != m);
    check_msg_identity (m, n);
    gcs_comp_msg_delete (m);

    // test gcs_comp_msg_member()
    ck_assert(NULL == gcs_comp_msg_member (n, -1));
    for (i = 0; i < memb_num; i++) {
        const char* id = gcs_comp_msg_member (n, i)->id;
        ck_assert(NULL != id);
        ck_assert(!strcmp(members[i].id, id));
    }
    ck_assert(NULL == gcs_comp_msg_member (n, i));

    // test gcs_comp_msg_idx()
    ck_assert(-1 == gcs_comp_msg_idx (n, ""));
    ck_assert(-1 == gcs_comp_msg_idx (n, long_id));
    for (i = 0; i < memb_num; i++)
        ck_assert(i == gcs_comp_msg_idx (n, members[i].id));

    // test gcs_comp_msg_primary()
    ck_assert(n->primary == gcs_comp_msg_primary(n));
}
END_TEST

Suite *gcs_comp_suite(void)
{
    Suite *suite = suite_create("GCS component message");
    TCase *tcase = tcase_create("gcs_comp");

    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase, gcs_comp_test);
    return suite;
}

