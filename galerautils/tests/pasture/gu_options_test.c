// Copyright (C) 2009 Codership Oy <info@codership.com>

// $Id$

#include <string.h>
#include <check.h>

#include "../src/gu_options.h"
#include "gu_options_test.h"

START_TEST (gu_options_test1)
{
    char key_sep  = '=';
    char pair_sep = ';';
    const char opts_str[] = " key0 = value0 ;; key1; key22 = ;key333=value333";

    struct gu_options* o1 = gu_options_from_string (opts_str,pair_sep,key_sep);

    fail_if (NULL == o1);
    fail_if (o1->opts_num != 4, "Expected 4 options, found: %ld", o1->opts_num);

    // 1st pair
    fail_if (strcmp (o1->opts[0].key.token, "key0"));
    fail_if (o1->opts[0].key.len != strlen ("key0"));
    fail_if (strcmp (o1->opts[0].value.token, "value0"));
    fail_if (o1->opts[0].value.len != strlen ("value0"));

    // 2nd pair
    fail_if (strcmp (o1->opts[1].key.token, "key1"));
    fail_if (o1->opts[1].key.len != strlen ("key1"));
    fail_if (o1->opts[1].value.token != NULL);
    fail_if (o1->opts[1].value.len != 0);

    // 3rd pair
    fail_if (strcmp (o1->opts[2].key.token, "key22"));
    fail_if (o1->opts[2].key.len != strlen ("key22"));
    fail_if (o1->opts[2].value.token != NULL);
    fail_if (o1->opts[2].value.len != 0);

    // 4th pair
    fail_if (strcmp (o1->opts[3].key.token, "key333"));
    fail_if (o1->opts[3].key.len != strlen ("key333"));
    fail_if (strcmp (o1->opts[3].value.token, "value333"));
    fail_if (o1->opts[3].value.len != strlen ("value333"));

    char* ostr = gu_options_to_string (o1, pair_sep, key_sep);

    struct gu_options* o2 = gu_options_from_string (ostr, pair_sep, key_sep);

    fail_if (NULL == o2);
    fail_if (o1->opts_num != o2->opts_num);

    // 1st pair
    fail_if (strcmp (o2->opts[0].key.token, "key0"));
    fail_if (o2->opts[0].key.len != strlen ("key0"));
    fail_if (strcmp (o2->opts[0].value.token, "value0"));
    fail_if (o2->opts[0].value.len != strlen ("value0"));

    // 2nd pair
    fail_if (strcmp (o2->opts[1].key.token, "key1"));
    fail_if (o2->opts[1].key.len != strlen ("key1"));
    fail_if (o2->opts[1].value.token != NULL);
    fail_if (o2->opts[1].value.len != 0);

    // 3rd pair
    fail_if (strcmp (o2->opts[2].key.token, "key22"));
    fail_if (o2->opts[2].key.len != strlen ("key22"));
    fail_if (o2->opts[2].value.token != NULL);
    fail_if (o2->opts[2].value.len != 0);

    // 4th pair
    fail_if (strcmp (o2->opts[3].key.token, "key333"));
    fail_if (o2->opts[3].key.len != strlen ("key333"));
    fail_if (strcmp (o2->opts[3].value.token, "value333"));
    fail_if (o2->opts[3].value.len != strlen ("value333"));

    gu_options_free (o1);
    gu_options_free (o2);
}
END_TEST

// test some error conditions
START_TEST (gu_options_test2)
{
    struct gu_options* o;

    o = gu_options_from_string ("", ';', '=');
    fail_if (NULL == o);
    fail_if (o->opts_num != 0);
    gu_options_free (o);

    o = gu_options_from_string (";;;;;", ';', '=');
    fail_if (NULL == o);
    fail_if (o->opts_num != 0);
    gu_options_free (o);

    o = gu_options_from_string ("key key =value = 0 ", ';', '=');
    fail_if (NULL == o);
    fail_if (o->opts_num != 1);
    fail_if (strcmp (o->opts[0].key.token, "key key"));
    fail_if (o->opts[0].key.len != strlen ("key key"));
    fail_if (strcmp (o->opts[0].value.token, "value = 0"));
    fail_if (o->opts[0].value.len != strlen ("value = 0"));
    gu_options_free (o);

    o = gu_options_from_string ("=", ';', '=');
    fail_if (NULL != o);

    o = gu_options_from_string (" = owfvwe;", ';', '=');
    fail_if (NULL != o);

    o = gu_options_from_string ("key = value; =", ';', '=');
    fail_if (NULL != o);

}
END_TEST

Suite *gu_options_suite(void)
{
  Suite *s   = suite_create("Galera options utils");
  TCase *tc1 = tcase_create("gu_opts");

  suite_add_tcase (s, tc1);
  tcase_add_test(tc1, gu_options_test1);
  tcase_add_test(tc1, gu_options_test2);

  return s;
}

