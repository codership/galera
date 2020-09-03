/*
 * Copyright (C) 2011-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcache_mem_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_mem_test.hpp"

using namespace gcache;

START_TEST(test1)
{
    ssize_t const bh_size (sizeof(gcache::BufferHeader));
    ssize_t const mem_size (3 + 2*bh_size);

    seqno2ptr_t s2p(SEQNO_NONE);
    MemStore ms(mem_size, s2p, 0);

    void* buf1 = ms.malloc (1 + bh_size);
    ck_assert(NULL != buf1);

    BufferHeader* bh1(ptr2BH(buf1));
    ck_assert(bh1->seqno_g == SEQNO_NONE);
    ck_assert(!BH_is_released(bh1));

    void* buf2 = ms.malloc (1 + bh_size);
    ck_assert(NULL != buf2);
    ck_assert(buf1 != buf2);

    void* buf3 = ms.malloc (1 + bh_size);
    ck_assert(NULL == buf3);

    buf1 = ms.realloc (buf1, 2 + bh_size);
    ck_assert(NULL != buf1);

    bh1 = ptr2BH(buf1);
    ck_assert(bh1->seqno_g == SEQNO_NONE);
    ck_assert(!BH_is_released(bh1));

    BufferHeader* bh2(ptr2BH(buf2));
    ck_assert(bh2->seqno_g == SEQNO_NONE);
    ck_assert(!BH_is_released(bh2));
    bh2->seqno_g = 1;

    /* freeing seqno'd buffer should only release it, but not discard */
    BH_release(bh2);
    ms.free (bh2);
    ck_assert(BH_is_released(bh2));

    buf3 = ms.malloc (1 + bh_size);
    ck_assert(NULL == buf3);

    /* discarding a buffer should finally free some space for another */
    ms.discard(bh2);

    buf3 = ms.malloc (1 + bh_size);
    ck_assert(NULL != buf3);

    /* freeing unseqno'd buffer should free space immeditely */
    bh1 = ptr2BH(buf1);
    BH_release(bh1);
    ms.free (bh1);

    void* buf4 = ms.malloc (2 + bh_size);
    ck_assert(NULL != buf4);

    BufferHeader* bh3(ptr2BH(buf3));
    BH_release(bh3);
    ms.free (bh3);

    BufferHeader* bh4(ptr2BH(buf4));
    BH_release(bh4);
    ms.free (bh4);

    ck_assert(!ms._allocd());
}
END_TEST

Suite* gcache_mem_suite()
{
    Suite* s = suite_create("gcache::MemStore");
    TCase* tc;

    tc = tcase_create("test");
    tcase_add_test(tc, test1);
    suite_add_tcase(s, tc);

    return s;
}
