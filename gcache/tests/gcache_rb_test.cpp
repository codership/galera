/*
 * Copyright (C) 2011-2014 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcache_rb_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_rb_test.hpp"

using namespace gcache;

START_TEST(test1)
{
    std::string const rb_name = "rb_test";
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
    ssize_t const rb_size (4 + 2*bh_size);

    std::map<int64_t, const void*> s2p;
    RingBuffer rb(rb_name, rb_size, s2p);

    fail_if (rb.size() != rb_size, "Expected %zd, got %zd", rb_size, rb.size());

    void* buf1 = rb.malloc (3 + bh_size);
    fail_if (NULL != buf1); // > 1/2 size

    buf1 = rb.malloc (1 + bh_size);
    fail_if (NULL == buf1);

    BufferHeader* bh1(ptr2BH(buf1));
    fail_if (bh1->seqno_g != SEQNO_NONE);
    fail_if (BH_is_released(bh1));

    void* buf2 = rb.malloc (2 + bh_size);
    fail_if (NULL == buf2);
    fail_if (BH_is_released(bh1));

    BufferHeader* bh2(ptr2BH(buf2));
    fail_if (bh2->seqno_g != SEQNO_NONE);
    fail_if (BH_is_released(bh2));

    void* tmp = rb.realloc (buf1, 2 + bh_size);
    fail_if (bh2->seqno_g != SEQNO_NONE);
    fail_if (NULL != tmp);

    BH_release(bh2);
    rb.free (bh2);

    tmp = rb.realloc (buf1, 2 + bh_size);
    fail_if (NULL != tmp);

    BH_release(bh1);
    rb.free (bh1);
    fail_if (!BH_is_released(bh1));

    buf1 = rb.malloc (1 + bh_size);
    fail_if (NULL == buf1);

    tmp = rb.realloc (buf1, 2 + bh_size);
    fail_if (NULL == tmp);
    fail_if (tmp != buf1);

    buf2 = rb.malloc (1 + bh_size);
    fail_if (NULL == buf2);

    tmp = rb.realloc (buf2, 2 + bh_size);
    fail_if (NULL == tmp);
    fail_if (tmp != buf2);

    tmp = rb.malloc (1 + bh_size);
    fail_if (NULL != tmp);

    BH_release(ptr2BH(buf1));
    rb.free(ptr2BH(buf1));

    BH_release(ptr2BH(buf2));
    rb.free(ptr2BH(buf2));

    tmp = rb.malloc (2 + bh_size);
    fail_if (NULL == tmp);

    mark_point();
}
END_TEST

Suite* gcache_rb_suite()
{
    Suite* ts = suite_create("gcache::RbStore");
    TCase* tc = tcase_create("test");

    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test1);
    suite_add_tcase(ts, tc);

    return ts;
}
