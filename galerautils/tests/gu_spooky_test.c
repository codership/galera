// Copyright (C) 2012 Codership Oy <info@codership.com>

// $Id$

#include "gu_spooky_test.h"

#include "../src/gu_sppoky.h"
#include "../src/gu_log.h"
#include "../src/gu_print_buf.h"

/* returns true if check fails */
static bool
check (const void* const exp, const void* const got, ssize_t size)
{
    if (memcmp (exp, got, size))
    {
        ssize_t str_size = size * 2.2 + 1;
        char c[str_size], r[str_size];

        gu_print_buf (exp, size, c, sizeof(c), false);
        gu_print_buf (got, size, r, sizeof(r), false);

        gu_info ("expected Spooky hash:\n%s\nfound:\n%s\n", c, r);

        return true;
    }

    return false;
}

START_TEST (gu_spooky_test)
{
}
END_TEST

Suite *gu_spooky_suite(void)
{
  Suite *s  = suite_create("Galera Spooky hash");
  TCase *tc = tcase_create("gu_spooky");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, gu_spooky_test);

  return s;
}

