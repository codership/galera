/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../gcs_node.hpp"

#include "gcs_node_test.hpp" // must be included last

#define NODE_ID "owpiefd[woie"
#define NODE_NAME "strange name"
#define NODE_ADDR "0.0.0.0:0"

START_TEST (gcs_node_test)
{
    /* this is a small unit test as node unit does almost nothing */
    gcs_node_t node1, node2;
    static const gcs_seqno_t seqno = 333;

    gcs_node_init (&node1, NULL, NODE_ID, NODE_NAME, NODE_ADDR, 0, 0, 0, 0);
    gcs_node_init (&node2, NULL, "baka", NULL, NULL, 0, 0, 0, 0);

    ck_assert_msg(!strcmp(node1.id, NODE_ID), "Expected node id '%s', "
                  "found '%s'", NODE_ID, node1.id);

    ck_assert_msg(!strcmp(node1.name, NODE_NAME), "Expected node name '%s', "
                  "found '%s'", NODE_NAME, node1.name);

    ck_assert_msg(!strcmp(node1.inc_addr, NODE_ADDR), "Expected node id '%s', "
                  "found '%s'", NODE_ADDR, node1.inc_addr);

    ck_assert(!gcs_node_get_last_applied(&node1));

    gcs_node_set_last_applied (&node1, seqno);

    mark_point();

    gcs_node_move (&node2, &node1);

    ck_assert_msg(seqno == gcs_node_get_last_applied (&node2),
                  "move didn't preserve last_applied");

    ck_assert_msg(!strcmp(node2.id, NODE_ID),
                  "Expected node id '%s', found '%s'", NODE_ID, node2.id);

    gcs_node_reset (&node1);

    mark_point();

    gcs_node_free  (&node2);
}
END_TEST

Suite *gcs_node_suite(void)
{
    Suite *suite = suite_create("GCS node context");
    TCase *tcase = tcase_create("gcs_node");

    suite_add_tcase (suite, tcase);
    tcase_add_test  (tcase, gcs_node_test);
    return suite;
}

