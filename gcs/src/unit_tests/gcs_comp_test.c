/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
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
#include "../gcs_comp_msg.h"
#include "gcs_comp_test.h"

static const char* members[] =
{
    "0",
    "88888888",
    "1",
    "7777777",
    "22",
    "666666",
    "333",
    "55555",
    "4444"
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
    }
}

START_TEST (gcs_comp_test)
{
    long memb_num     = sizeof(members)/sizeof(char*);
    long my_idx       = getpid() % memb_num;
    long prim         = my_idx % 2;
    gcs_comp_msg_t* m = gcs_comp_msg_new (prim, my_idx, memb_num);
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
	ret = gcs_comp_msg_add (m, members[i]);
	fail_if (ret != i, "gcs_comp_msg_add() returned %d, expected %d",
		 ret, i);
    }

    // try to add a id that was added already
    if (my_idx < i) {
	j = my_idx;
    } else {
	j = i - 1;
    }
    ret = gcs_comp_msg_add (m, members[j]);
    fail_if (ret != -ENOTUNIQ, "gcs_comp_msg_add() returned %d, expected "
	     "-ENOTUNIQ (%d)", ret, -ENOTUNIQ);

    // try to add empty id
    ret = gcs_comp_msg_add (m, "");
    fail_if (ret != -EINVAL, "gcs_comp_msg_add() returned %d, expected "
	     "-EINVAL (%d)", ret, -EINVAL);

    // try to add id that is too long
    ret = gcs_comp_msg_add (m, long_id);
    fail_if (ret != -ENAMETOOLONG, "gcs_comp_msg_add() returned %d, expected "
	     "-ENAMETOOLONG (%d)", ret, -ENAMETOOLONG);

    // add final id
    ret = gcs_comp_msg_add (m, members[i]);
    fail_if (ret != i, "gcs_comp_msg_add() returned %d, expected %d",
	     ret, i);

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

    // test gcs_comp_msg_id()
    fail_unless (NULL == gcs_comp_msg_id (n, -1));
    for (i = 0; i < memb_num; i++) {
	const char* id = gcs_comp_msg_id (n, i);
	fail_if (NULL == id);
	fail_if (strcmp(members[i], id));
    }
    fail_unless (NULL == gcs_comp_msg_id (n, i));

    // test gcs_comp_msg_idx()
    fail_if (-1 != gcs_comp_msg_idx (n, ""));
    fail_if (-1 != gcs_comp_msg_idx (n, long_id));
    for (i = 0; i < memb_num; i++)
	fail_if (i != gcs_comp_msg_idx (n, members[i]));

    // test gcs_comp_msg_primary()
    fail_if (n->primary != gcs_comp_msg_primary(n));
}
END_TEST

Suite *gcs_comp_suite(void)
{
    Suite *suite = suite_create("GCS Component Message");
    TCase *tcase = tcase_create("gcs_comp");

    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase, gcs_comp_test);
    return suite;
}

