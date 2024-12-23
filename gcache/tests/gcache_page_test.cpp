/*
 * Copyright (C) 2010-2025 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GCACHE_PAGE_STORE_UNIT_TEST

#include "gcache_page_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_limits.hpp"
#include "gcache_page_test.hpp"

#include <vector>
#include <gu_inttypes.hpp>

using namespace gcache;

static int const DEBUG = 4; // page store debug flag

class SeqnoMapStub : public SeqnoMap
{
public:
    SeqnoMapStub() : last_seqno_(0), low_limit_(SEQNO_NONE) {}
    void seqno_discard(const seqno_t& seqno)
    {
        assert(seqno > 0);
        /* make sure set_low_limit() is called before seqno_discard() */
        ck_assert_msg(seqno <= low_limit_,
                      "Discard seqno: %" PRId64 ", low_limit: %" PRId64,
                      seqno, low_limit_);
        if (last_seqno_ < seqno) last_seqno_ = seqno;
    }
    void set_low_limit(const seqno_t& seqno)
    {
        log_info << "set_low_limit(" << seqno << ")";
        low_limit_ = std::max(low_limit_, seqno);
    }
    seqno_t low_limit() const { return low_limit_; }
    seqno_t seqno_discarded() const { return last_seqno_; }
private:
    seqno_t last_seqno_;
    seqno_t low_limit_;
};

static
void ps_free (gcache::PageStore& ps, void* ptr)
{
    BufferHeader* const bh(ptr2BH(ptr));
    BH_release (bh);
    ps.free(bh);
}

START_TEST(test1)
{
    log_info << "\n#\n# test1\n#";
    const char* const dir_name = "";
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
    ssize_t const keep_size = 1;
    ssize_t const page_size = 2 + bh_size;

    SeqnoMapStub sm;

    gcache::PageStore ps (sm, dir_name, keep_size, page_size, DEBUG, false);

    ck_assert_msg(ps.count()       == 0,"expected count 0, got %zu",ps.count());
    ck_assert_msg(ps.total_pages() == 0,"expected 0 pages, got %zu",ps.total_pages());
    ck_assert_msg(ps.total_size()  == 0,"expected size 0, got %zu", ps.total_size());

    void* buf = ps.malloc (3 + bh_size);

    ck_assert(0 != buf);
    ck_assert_msg(ps.count()       == 1,"expected count 1, got %zu",ps.count());
    ck_assert_msg(ps.total_pages() == 1,"expected 1 pages, got %zu",ps.total_pages());

    void* tmp = ps.realloc (buf, 2 + bh_size);

    ck_assert(buf == tmp);
    ck_assert_msg(ps.count()       == 1,"expected count 1, got %zu",ps.count());
    ck_assert_msg(ps.total_pages() == 1,"expected 1 pages, got %zu",ps.total_pages());

    tmp = ps.realloc (buf, 4 + bh_size); // here new page should be allocated

    ck_assert(0   != tmp);
    ck_assert(buf != tmp);
    ck_assert_msg(ps.count()       == 2,"expected count 2, got %zu",ps.count());
    ck_assert_msg(ps.total_pages() == 1,"expected 1 pages, got %zu",ps.total_pages());

    ps_free(ps, tmp);

    ck_assert_msg(ps.count()       == 2,"expected count 2, got %zu",ps.count());
    ps.wait_page_discard();
    ck_assert_msg(ps.total_pages() == 0,"expected 0 pages, got %zu",ps.total_pages());
    ck_assert_msg(ps.total_size()  == 0,"expected size 0, got %zu", ps.total_size());

    mark_point();
}
END_TEST

START_TEST(test2)
{
    log_info << "\n#\n# test2\n#";
    const char* const dir_name = "";
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
    ssize_t const keep_size = 1;
    ssize_t page_size = (1 << 20) + bh_size;

    SeqnoMapStub sm;

    gcache::PageStore ps (sm, dir_name, keep_size, page_size, 0, false);

    mark_point();

    uint8_t* buf = static_cast<uint8_t*>(ps.malloc (page_size));

    ck_assert(0 != buf);

    while (--page_size)
    {
        buf[page_size] = page_size;
    }

    mark_point();

    ps_free(ps, buf);

    mark_point();
}
END_TEST

START_TEST(test3) // check that all page size is efficiently used
{
    log_info << "\n#\n# test3\n#";
    const char* const dir_name = "";
    ssize_t const keep_size = 1;
    ssize_t const page_size = 1024;

    SeqnoMapStub sm;

    {
    gcache::PageStore ps (sm, dir_name, keep_size, page_size, 0, false);

    mark_point();

    ssize_t ptr_size = (page_size / 2);

    void* ptr1 = ps.malloc (ptr_size);
    ck_assert(0 != ptr1);

    void* ptr2 = ps.malloc (ptr_size);
    ck_assert(0 != ptr2);

    ck_assert_msg(ps.count() == 1, "ps.count() = %zd, expected 1",
                  ps.count());

    // check that ptr2 is adjacent to ptr1
    void* tmp = static_cast<uint8_t*>(ptr1) + ptr_size;

    ck_assert_msg(tmp == ptr2, "tmp = %p, ptr2 = %p", tmp, ptr2);

    ps_free(ps, ptr1);
    ps_free(ps, ptr2);
    }

    mark_point();
}
END_TEST

static void
test_caching_fill_page(gcache::PageStore&  ps,
                       std::vector<void*>& buf,
                       size_t const        expected_page_count)
{
    for (int i(0); i < 3; i++)
    {
        buf.push_back(ps.malloc(gcache::Limits::MIN_SIZE));
        void* const ptr(buf[buf.size() - 1]);
        ck_assert(nullptr != ptr);
        ck_assert(SEQNO_NONE == ptr2BH(ptr)->seqno_g);
        ck_assert_msg(ps.count() == expected_page_count,
                      "buf = %zu, ps.count() = %zu (expected %zu)",
                      buf.size()-1, ps.count(), expected_page_count);
    }
}

START_TEST(test_caching) // test that caching in pages work
{
    log_info << "\n#\n# test_caching\n#";
    const char* const dir_name = "";
    size_t const bh_size = sizeof(gcache::BufferHeader);
    size_t const page_size = (8 + bh_size)*3; // fits 3 buffers <= 8 bytes
    size_t const keep_size = 2*page_size;     // keep at least 2 pages

    std::vector<void*> buf;

    SeqnoMapStub sm;

    gcache::PageStore ps(sm, dir_name, keep_size, page_size, DEBUG, false);

    mark_point();

    /*
     * 1. Populate 6 pages
     */
    for (size_t page_count(1); page_count <= 6; page_count++)
        test_caching_fill_page(ps, buf, page_count);

    ck_assert(6 == ps.total_pages());
    ck_assert(18 == buf.size());

    /*
     * 2. Free some "unused" buffers and assign seqnos out of order to
     *    others
     */
    ps_free(ps, buf[0]);
    ps_free(ps, buf[1]);
    ps_free(ps, buf[2]);
    ps.wait_page_discard(); // one page should go
    ck_assert_msg(5 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ps.wait_page_discard();
    ck_assert(sm.low_limit() == 0);
    ck_assert(sm.seqno_discarded() == 0);

    ps_free(ps, buf[3]);
    ps.seqno_assign(ptr2BH(buf[4]), 1); ps_free(ps, buf[4]);
    ps_free(ps, buf[5]);
    ps.wait_page_discard(); // one page should go
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 1);

    ps_free(ps, buf[6]);
    ps.seqno_assign(ptr2BH(buf[7]), 4); // this should pin the page
    ps_free(ps, buf[8]);
    ps.wait_page_discard();
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 1);

    ps_free(ps, buf[9]);
    ps.seqno_assign(ptr2BH(buf[11]), 2);
    ps.seqno_assign(ptr2BH(buf[10]), 3); ps_free(ps, buf[10]);
    ps.wait_page_discard();
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());

    ps_free(ps, buf[11]);
    ps.wait_page_discard();
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());

    ps_free(ps, buf[7]);
    ps.wait_page_discard(); // this should free 2 pages at once
    ck_assert_msg(2 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 4);

    /*
     * 3. Test that the last 2 pages will remain even after freeing
     */
    ps_free(ps, buf[12]);
    ps.seqno_assign(ptr2BH(buf[13]), 5); ps_free(ps, buf[13]);
    ps.seqno_assign(ptr2BH(buf[14]), 6); ps_free(ps, buf[14]);
    ps.wait_page_discard(); // all pages should stay
    ck_assert_msg(2 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 4);

    ps.seqno_assign(ptr2BH(buf[15]), 7); ps_free(ps, buf[15]);
    ps.seqno_assign(ptr2BH(buf[16]), 8); ps_free(ps, buf[16]);
    ps_free(ps, buf[17]);
    ps.wait_page_discard(); // all pages should stay
    ck_assert_msg(2 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 4);

    /*
     * 4. Test that new page allocation caused by new malloc() will cause
     *    the oldest page to go
     */
    size_t const old_page_count(ps.count());
    buf.push_back(ps.malloc(gcache::Limits::MIN_SIZE));
    ck_assert(old_page_count + 1 == ps.count()); // new page was allocated
    ck_assert_msg(2 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 6);

    for (size_t i(18); i < buf.size(); i++)
    {
        ps_free(ps, buf[i]);
    }

    mark_point();
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
    tcase_add_test(tc, test_caching);
    suite_add_tcase(s, tc);

    return s;
}
