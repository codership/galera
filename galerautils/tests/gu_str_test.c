/*
 * Copyright (c) 2010-2020 Codership Oy <www.codership.com>
 */

#include "gu_str.h"

#include <check.h>

START_TEST(test_append)
{
    const char* strs[3] = {
        "t",
        "ttt",
        "tttttttt"
    };
    char* str = NULL;
    size_t off = 0;
    size_t i;
    for (i = 0; i < 3; ++i)
    {
        str = gu_str_append(str, &off, strs[i], strlen(strs[i]));
    }

    free(str);
}
END_TEST


START_TEST(test_scan)
{
    const char* strs[5] = {
        "1",
        "234",
        "56789abc",
        "4657777777777",
        "345"
    };
    char* str = NULL;
    size_t off = 0;
    size_t len = 0;
    size_t i;
    const char* ptr;
    for (i = 0; i < 5; ++i)
    {
        str = gu_str_append(str, &off, strs[i], strlen(strs[i]));
        len += strlen(strs[i]) + 1;
    }

    ptr = str;
    for (i = 0; i < 5; ++i)
    {
        ck_assert(strcmp(ptr, strs[i]) == 0);
        ptr = gu_str_next(ptr);
    }
    ck_assert(ptr == len + str);

    for (i = 0; i < 5; ++i)
    {
        ptr = gu_str_advance(str, i);
        ck_assert(strcmp(ptr, strs[i]) == 0);
    }

    free(str);
}
END_TEST

START_TEST(test_str_table)
{
    size_t n_cols = 5;
    char const* col_names[5] = {
        "col1", "column2", "foo", "bar", "zzz"
    };
    size_t n_rows = 255;
    const char* row[5] = {"dddd", "asdfasdf", "sadfdf", "", "a"};

    const char* name = "test_table";

    char* str = NULL;
    size_t off = 0;
    size_t i;

    str = gu_str_table_set_name(str, &off, name);
    ck_assert(strcmp(gu_str_table_get_name(str), name) == 0);

    str = gu_str_table_set_n_cols(str, &off, n_cols);
    ck_assert(gu_str_table_get_n_cols(str) == n_cols);

    str = gu_str_table_set_n_rows(str, &off, n_rows);
    ck_assert(gu_str_table_get_n_rows(str) == n_rows);

    str = gu_str_table_set_cols(str, &off, n_cols, col_names);

    for (i = 0; i < n_rows; ++i)
    {
        str = gu_str_table_append_row(str, &off, n_cols, row);
    }

    mark_point();

    FILE* tmp = fopen("/dev/null", "w");
    ck_assert(NULL != tmp);

    gu_str_table_print(tmp, str);

    fclose(tmp);

    free(str);
}
END_TEST


Suite* gu_str_suite()
{
    Suite* s = suite_create("Galera Str util suite");
    TCase* tc;

    tc = tcase_create("test_append");
    tcase_add_test(tc, test_append);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_scan");
    tcase_add_test(tc, test_scan);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_str_table");
    tcase_add_test(tc, test_str_table);
    suite_add_tcase(s, tc);

    return s;
}
