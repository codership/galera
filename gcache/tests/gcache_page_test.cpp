/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcache_page_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_page_test.hpp"

using namespace gcache;

void ps_free (void* ptr)
{
    BufferHeader* const bh(ptr2BH(ptr));
    BH_release (bh);
    bh->seqno_g = SEQNO_ILL;
}

START_TEST(test1)
{
    const char* const dir_name = "";
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
    ssize_t const keep_size = 1;
    ssize_t const page_size = 2 + bh_size;

    gcache::PageStore ps (dir_name, keep_size, page_size, false);

    mark_point();

    void* buf = ps.malloc (3 + bh_size);

    fail_if (0 == buf);

    void* tmp = ps.realloc (buf, 2 + bh_size);

    fail_if (buf != tmp);

    tmp = ps.realloc (buf, 4 + bh_size); // here new page should be allocated

    fail_if (0 == tmp);
    fail_if (buf == tmp);

    ps_free(tmp);
    ps.discard (ptr2BH(tmp));
}
END_TEST

START_TEST(test2)
{
    const char* const dir_name = "";
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
    ssize_t const keep_size = 1;
    ssize_t page_size = (1 << 20) + bh_size;

    gcache::PageStore ps (dir_name, keep_size, page_size, false);

    mark_point();

    uint8_t* buf = static_cast<uint8_t*>(ps.malloc (page_size));

    fail_if (0 == buf);

    while (--page_size)
    {
        buf[page_size] = page_size;
    }

    mark_point();

    ps_free(buf);
    ps.discard (ptr2BH(buf));
}
END_TEST

START_TEST(test3) // check that all page size is efficiently used
{
    const char* const dir_name = "";
    ssize_t const keep_size = 1;
    ssize_t const page_size = 1024;

    gcache::PageStore ps (dir_name, keep_size, page_size, false);

    mark_point();

    ssize_t ptr_size = (page_size / 2);

    void* ptr1 = ps.malloc (ptr_size);
    fail_if (0 == ptr1);

    void* ptr2 = ps.malloc (ptr_size);
    fail_if (0 == ptr2);

    fail_if (ps.count() != 1, "ps.count() = %zd, expected 1", ps.count());

    // check that ptr2 is adjacent to ptr1
    void* tmp = static_cast<uint8_t*>(ptr1) + ptr_size;

    fail_if (tmp != ptr2, "tmp = %p, ptr2 = %p", tmp, ptr2);
}
END_TEST

Suite* gcache_page_suite()
{
    Suite* s = suite_create("gcache::PageStore");
    TCase* tc;

    tc = tcase_create("test");
    tcase_add_test(tc, test1);
    tcase_add_test(tc, test2);
    tcase_add_test(tc, test3);
    suite_add_tcase(s, tc);

    return s;
}
