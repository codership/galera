/*
 * Copyright (C) 2025 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GCACHE_UNIT_TEST
#define GCACHE_PAGE_STORE_UNIT_TEST

#include "GCache.hpp"
#include "gcache_limits.hpp"
#include "gcache_top_test.hpp"

#include <vector>
#include <gu_inttypes.hpp>

using namespace gcache;

static int const DEBUG = 4;

static void
test_caching_fill_page(gcache::GCache&     gc,
                       std::vector<void*>& buf,
                       size_t const        expected_page_count)
{
    const PageStore& ps(gc.page_store());
    for (int i(0); i < 3; i++)
    {
        buf.push_back(gc.malloc(1));
        void* const ptr(buf[buf.size() - 1]);
        ck_assert(nullptr != ptr);
        ck_assert(SEQNO_NONE == ptr2BH(ptr)->seqno_g);
        ck_assert_msg(ps.count() == expected_page_count,
                      "%d. buf = %zu, ps.count() = %zu (expected %zu)",
                      i, buf.size()-1, ps.count(), expected_page_count);
    }
}

START_TEST(top_level_page_caching) // test that caching in pages work
{
    log_info << "\n#\n# top_level_page_caching\n#";
    const char* const dir_name = "";
    size_t const bh_size = sizeof(gcache::BufferHeader);
    size_t const page_size = (8 + bh_size)*3; // fits 3 buffers <= 8 bytes
    size_t const keep_size = 2*page_size;     // keep at least 2 pages

    gu::Config cfg;
    GCache::register_params(cfg);
    cfg.set("gcache.dir", dir_name);
    cfg.set("gcache.size", 0); // turn off ring buffer
    cfg.set("gcache.page_size", page_size);
    cfg.set("gcache.keep_pages_size", keep_size);
#ifndef NDEBUG
    cfg.set("gcache.debug", DEBUG);
#endif

    GCache gc(nullptr, cfg, dir_name);
    const seqno2ptr_t& sm(gc.seqno_map());
    const PageStore&   ps(gc.page_store());
    ck_assert_msg(ps.page_size() == page_size,
                  "ps.page_size: %zu (expected %zu)",
                  ps.page_size(), page_size);

    std::vector<void*> buf;

    mark_point();

    /*
     * 1. Populate 6 pages
     */
    for (size_t page_count(1); page_count <= 6; page_count++)
        test_caching_fill_page(gc, buf, page_count);
    ck_assert_msg(ps.total_pages() == 6,
                  "total_pages %zu (expected 6)", ps.total_pages());

    /*
     * 2. Free some "unused" buffers and assign seqnos out of order to others
     */
    gc.free(buf[0]);
    gc.free(buf[1]);
    gc.free(buf[2]);
    ps.wait_page_discard(); // 1st page should go
    ck_assert_msg(ps.total_pages() == 5,
                  "total_pages %zu (expected 5)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == -1,
                  "seqno_min: %" PRId64 " (expected -1)", gc.seqno_min());

    gc.free(buf[3]);
    gc.seqno_assign(buf[4], 1, 0, false); gc.seqno_release(1);
    ck_assert(gc.seqno_min() == 1);
    gc.free(buf[5]);
    ps.wait_page_discard(); // 2nd page should go
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 4)", ps.total_pages());
    ck_assert_msg(sm.empty() == true,
                  "SM size: %zu, begin: %" PRId64 ", end: %" PRId64
                  ", front: %p, back: %p"
                  ,sm.size(), sm.index_begin(), sm.index_end()
                  ,sm.front(), sm.back()
        );
    ck_assert_msg(gc.seqno_min() == -1,
                  "seqno_min: %" PRId64 " (expected -1)", gc.seqno_min());

    gc.free(buf[6]);
    gc.seqno_assign(buf[7], 4, 0, false); // this should pin the page
    gc.free(buf[8]);
    ck_assert_msg(gc.seqno_min() == 4,
                  "seqno_min: %" PRId64 " (expected 4)", gc.seqno_min());
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 4)", ps.total_pages());

    gc.free(buf[9]);
    gc.seqno_assign(buf[11], 2, 0, false); gc.seqno_release(2);
    ck_assert(gc.seqno_min() == 2);
    gc.seqno_assign(buf[10], 3, 0, false); gc.seqno_release(3);
    // page 3 should stay since seqno 4 is not released, and therefore page 4
    ps.wait_page_discard();
    ck_assert(gc.seqno_min() == 2);
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 4)", ps.total_pages());

    gc.seqno_release(4);
    // only after releasing seqno 4 all other buffers and pages 3 and 4 may go
    ps.wait_page_discard();
    ck_assert_msg(ps.total_pages() == 2,
                  "total_pages %zu (expected 2)", ps.total_pages());
    ck_assert(gc.seqno_min() == -1);

    /*
     * 3. Test that the last 2 pages will remain even after freeing
     */
    gc.free(buf[12]);
    gc.seqno_assign(buf[13], 5, 0, false);
    gc.seqno_assign(buf[14], 6, 0, false);
    ck_assert(gc.seqno_min() == 5);

    gc.seqno_assign(buf[15], 7, 0, false);
    gc.seqno_assign(buf[16], 8, 0, false);
    gc.seqno_release(8); // releases all previous seqnos as well
    gc.free(buf[17]);
    ps.wait_page_discard();
    ck_assert(gc.seqno_min() == 5);
    ck_assert_msg(ps.total_pages() == 2,
                  "total_pages %zu (expected 2)", ps.total_pages());

    buf.push_back(gc.malloc(1));
    // this shall allocate one more page and page 5 should go,
    // only 6 and 7 should remain
    ck_assert(ps.count() == 7);
    ps.wait_page_discard();
    ck_assert_msg(ps.total_pages() == 2,
                  "total_pages %zu (expected 2)", ps.total_pages());
    ck_assert(gc.seqno_min() == 7);

    for (size_t i(18); i < buf.size(); i++)
    {
        gc.free(buf[i]);
    }

    mark_point();
}
END_TEST

START_TEST(top_level_page_caching_locking) // test that caching in pages work
{
    log_info << "\n#\n# top_level_page_caching_locking\n#";
    const char* const dir_name = "";
    size_t const bh_size = sizeof(gcache::BufferHeader);
    size_t const page_size = (8 + bh_size)*3; // fits 3 buffers <= 8 bytes
    size_t const keep_size = 2*page_size;     // keep at least 2 pages

    gu::Config cfg;
    GCache::register_params(cfg);
    cfg.set("gcache.dir", dir_name);
    cfg.set("gcache.size", 0); // turn off ring buffer
    cfg.set("gcache.page_size", page_size);
    cfg.set("gcache.keep_pages_size", keep_size);
#ifndef NDEBUG
    cfg.set("gcache.debug", DEBUG);
#endif

    GCache gc(nullptr, cfg, dir_name);
    const PageStore&   ps(gc.page_store());
    ck_assert_msg(ps.page_size() == page_size,
                  "ps.page_size: %zu (expected %zu)",
                  ps.page_size(), page_size);

    std::vector<void*> buf;

    mark_point();

    /*
     * 1. Populate 5 pages
     */
    for (size_t page_count(1); page_count <= 5; page_count++)
        test_caching_fill_page(gc, buf, page_count);
    ck_assert_msg(ps.total_pages() == 5,
                  "total_pages %zu (expected 5)", ps.total_pages());

    /*
     * 2. Assign seqnos
     */
    for (size_t seqno(1); seqno <= buf.size(); seqno++)
        gc.seqno_assign(buf[seqno - 1], seqno, 0, false);
    ck_assert(gc.seqno_min() == 1);

    /*
     * 3. Lock seqno 5, it should hold page 2
     */
    gc.seqno_lock(5);

    gc.seqno_release(1);
    gc.seqno_release(2);
    ps.wait_page_discard();
    ck_assert_msg(ps.total_pages() == 5,
                  "total_pages %zu (expected 5)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == 1,
                  "seqno_min: %" PRId64 " (expected 1)", gc.seqno_min());
    gc.seqno_release(3);
    ps.wait_page_discard(); // page 1 should go
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 4)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == 4,
                  "seqno_min: %" PRId64 " (expected 4)", gc.seqno_min());
    try {
        gc.seqno_lock(3);
        ck_abort_msg("Should fail.");
    } catch (gu::NotFound&) {}

    gc.seqno_release(6);    // batch-releases all seqnos <= 6
    ps.wait_page_discard(); // page 2 should be held by a lock
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 5)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == 4,
                  "seqno_min: %" PRId64 " (expected 4)", gc.seqno_min());

    /*
     * 4. Lock seqno 4 to test that nothing changed
     */
    gc.seqno_lock(4);

    gc.seqno_release(9);
    ps.wait_page_discard(); // page 2 should be held by a lock
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 5)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == 4,
                  "seqno_min: %" PRId64 " (expected 4)", gc.seqno_min());

    /* and release page 4 just to see that it doesn't go below because it is
     * within the keep_pages_size */
    gc.seqno_release(12);

    /*
     * 5. Unlock seqnos. Only after the second unlock pages should be discarded
     */
    gc.seqno_unlock();
    ps.wait_page_discard();
    ck_assert_msg(ps.total_pages() == 4,
                  "total_pages %zu (expected 4)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == 4,
                  "seqno_min: %" PRId64 " (expected 4)", gc.seqno_min());

    gc.seqno_unlock();
    ps.wait_page_discard(); // pages 2 and 3 should be discarded
    ck_assert_msg(ps.total_pages() == 2,
                  "total_pages %zu (expected 2)", ps.total_pages());
    ck_assert_msg(gc.seqno_min() == 10,
                  "seqno_min: %" PRId64 " (expected 10)", gc.seqno_min());

    for (size_t seqno(10); seqno <= buf.size(); seqno++)
    {
        gc.seqno_release(seqno);
    }

    mark_point();
}
END_TEST

Suite* gcache_top_suite()
{
    Suite* s = suite_create("gcache::top-level");
    TCase* tc;

    tc = tcase_create("test");
    tcase_add_test(tc, top_level_page_caching);
    tcase_add_test(tc, top_level_page_caching_locking);
    suite_add_tcase(s, tc);

    return s;
}
