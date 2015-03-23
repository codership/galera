/*
 * Copyright (C) 2008-2015 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <galerautils.h>

#define  GCS_COMP_MSG_ACCESS
#include "../gcs_comp_msg.hpp"

#include "gcs_comp_test.hpp" // must be included last


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

    fail_if (n->primary  != m->primary);
    fail_if (n->my_idx   != m->my_idx);
    fail_if (n->memb_num != m->memb_num);
    for (i = 0; i < m->memb_num; i++) {
        fail_if (strlen(n->memb[i].id) != strlen(m->memb[i].id),
                 "member %d id len does not match: %d vs %d",
                 i, strlen(n->memb[i].id), strlen(m->memb[i].id));
        fail_if (strncmp (n->memb[i].id, m->memb[i].id,
                          GCS_COMP_MEMB_ID_MAX_LEN),
                 "member %d IDs don't not match: got '%s', should be '%s'",
                 i, members[i], m->memb[i].id);
        fail_if (n->memb[i].segment != m->memb[i].segment,
                 "member %d segments don't not match: got '%d', should be '%d'",
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

    fail_if (NULL == m);
    fail_if (memb_num != gcs_comp_msg_num  (m));
    fail_if (my_idx   != gcs_comp_msg_self (m));

    // add members except for the last
    for (i = 0; i < memb_num - 1; i++) {
        ret = gcs_comp_msg_add (m, members[i].id, members[i].segment);
        fail_if (ret != i, "gcs_comp_msg_add() returned %d, expected %d",
                 ret, i);
    }

    // try to add a id that was added already
    if (my_idx < i) {
        j = my_idx;
    } else {
        j = i - 1;
    }
    ret = gcs_comp_msg_add (m, members[j].id, members[j].segment);
    fail_if (ret != -ENOTUNIQ, "gcs_comp_msg_add() returned %d, expected "
             "-ENOTUNIQ (%d)", ret, -ENOTUNIQ);

    // try to add empty id
    ret = gcs_comp_msg_add (m, "", 0);
    fail_if (ret != -EINVAL, "gcs_comp_msg_add() returned %d, expected "
             "-EINVAL (%d)", ret, -EINVAL);

    // try to add id that is too long
    ret = gcs_comp_msg_add (m, long_id, 3);
    fail_if (ret != -ENAMETOOLONG, "gcs_comp_msg_add() returned %d, expected "
             "-ENAMETOOLONG (%d)", ret, -ENAMETOOLONG);

    // add final id
    ret = gcs_comp_msg_add (m, members[i].id, members[i].segment);
    fail_if (ret != i, "gcs_comp_msg_add() returned %d, expected %d",
             ret, i);

    // check that all added correctly
    for (i = 0; i < memb_num; i++) {
        const char* const id = gcs_comp_msg_member(m, i)->id;
        fail_if (strcmp (members[i].id, id),
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
    fail_if (NULL == m);
    check_msg_identity (m, n);
    gcs_comp_msg_delete (m);

    // test gcs_comp_msg_member()
    fail_unless (NULL == gcs_comp_msg_member (n, -1));
    for (i = 0; i < memb_num; i++) {
        const char* id = gcs_comp_msg_member (n, i)->id;
        fail_if (NULL == id);
        fail_if (strcmp(members[i].id, id));
    }
    fail_unless (NULL == gcs_comp_msg_member (n, i));

    // test gcs_comp_msg_idx()
    fail_if (-1 != gcs_comp_msg_idx (n, ""));
    fail_if (-1 != gcs_comp_msg_idx (n, long_id));
    for (i = 0; i < memb_num; i++)
        fail_if (i != gcs_comp_msg_idx (n, members[i].id));

    // test gcs_comp_msg_primary()
    fail_if (n->primary != gcs_comp_msg_primary(n));
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

