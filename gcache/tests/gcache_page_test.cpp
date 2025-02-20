/*
 * Copyright (C) 2010-2025 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GCACHE_PAGE_STORE_UNIT_TEST

#include "gcache_test_encryption.hpp"
#include "gcache_page_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_limits.hpp"
#include "gcache_page_test.hpp"

#include <gu_digest.hpp>
#include <gu_inttypes.hpp>
#include <vector>

using namespace gcache;

/* helper to switch between encryption and non-encryption modes */
class get_BH
{
public:
    get_BH(gcache::PageStore& ps, bool enc) : ps_(ps), enc_(enc) {}
    BufferHeader* operator()(const void* ptr, bool change = false) const
    {
        if (enc_) return ps_.get_BH(ptr, change); else return ptr2BH(ptr);
    }
private:
    gcache::PageStore& ps_;
    bool         const enc_;
}; /* class get_BH */

gcache::Page::EncKey const Key = { 1, 2, 3 };

static void log_test(int const n, bool const enc)
{
    log_info << "\n\n"
        "##########################\n"
        "##                      ##\n"
        "##        Test " << n << (enc ? 'E' : ' ') << "       ##\n"
        "##                      ##\n"
        "##########################\n";
}

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

static void ps_free (PageStore&    ps,
                     BufferHeader* bh,
                     void*   const ptr)
{
    BH_release (bh);
    ps.free(bh, ptr);
}

static void
t1(wsrep_encrypt_cb_t cb, void* app_ctx, const gcache::Page::EncKey& key)
{
    bool const enc(NULL != cb);
    log_test(1, enc);

    const char* const dir_name = "";
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
    ssize_t const keep_size = 1;
    ssize_t const page_size = 2 + bh_size + gcache::Page::meta_size(BH_size(0));

    SeqnoMapStub sm;
    gcache::PageStore ps(sm, dir_name, cb, app_ctx, keep_size, page_size, page_size,
                         0, false);

    ck_assert_msg(ps.count()       == 0,"expected count 0, got %zu (enc=%d)",
                  ps.count(), enc);
    ck_assert_msg(ps.total_pages() == 0,"expected 0 pages, got %zu",ps.total_pages());
    ck_assert_msg(ps.total_size()  == 0,"expected size 0, got %zu", ps.total_size());

    ps.set_enc_key(key);

    get_BH const BH(ps, enc);

    char data[3] = { 1, 2, 3 };
    void* ptx;
    size_t size(sizeof(data) + bh_size);
    void* buf(ps.malloc(size, ptx));

    ck_assert(NULL != buf);
    ck_assert(NULL != ptx);
    ck_assert_msg(ps.count()       == 1,"expected count 1, got %zu (enc=%d)",
                  ps.count(), enc);
    ck_assert_msg(ps.total_pages() == 1,"expected 1 pages, got %zu",ps.total_pages());

    ::memset(buf, 0, sizeof(data));/* initialize just for the sake of the test */
    ::memcpy(ptx, data, sizeof(data));

    if (!enc) // in-place realloc is not supported for encryuption
    {
        ck_assert(buf == ptx);

        size -= 1;
        void* tmp = ps.realloc (buf, size);

        ck_assert(buf == tmp);
        ck_assert_msg(ps.count()       == 1,
                      "expected count 1, got %zu", ps.count());
        ck_assert_msg(ps.total_pages() == 1,
                      "expected 1 pages, got %zu", ps.total_pages());

        size += gcache::Page::ALIGNMENT;
        // the following should fail as new page needs to be allocated
        tmp = ps.realloc (buf, size);

        ck_assert(0   == tmp);
        ck_assert(buf != tmp);
        ck_assert_msg(ps.count()       == 1,
                      "expected count 1, got %zu", ps.count());
        ck_assert_msg(ps.total_pages() == 1,
                      "expected 1 pages, got %zu", ps.total_pages());
    }
    else
    {
        ck_assert(buf != ptx);
        /* the following has a probability of failure 1/16M due to a certain
         * randomization in PageStore constructor... */
        ck_assert(0 != ::memcmp(buf, ptx, sizeof(data)));
    }

    if (enc)
    {
        const void* ptc(ps.get_plaintext(buf));
        ck_assert(0 != ::memcmp(buf,  ptc, sizeof(data)));
        ck_assert(0 == ::memcmp(data, ptc, sizeof(data)));
        ps.drop_plaintext(buf);
    }

    mark_point();

    ps.wait_page_discard();
    ck_assert_msg(ps.count()       == 1,"expected count 1, got %zu",ps.count());
    ck_assert_msg(ps.total_pages() == 1,"expected 1 pages, got %zu",ps.total_pages());

    ps_free(ps, BH(buf), buf); /* this shall flush plaintext in case of encryption
                                * and free ptx */

    ps.wait_page_discard();
    ck_assert_msg(ps.count()       == 1,"expected count 1, got %zu",ps.count());
    ck_assert_msg(ps.total_pages() == 0,"expected 0 pages, got %zu (enc=%d)",ps.total_pages(), enc);
    ck_assert_msg(ps.total_size()  == 0,"expected size 0, got %zu", ps.total_size());

    mark_point();
}

START_TEST(test1)
{
    t1(NULL, NULL, Key);
    t1(gcache_test_encrypt_cb, NULL, Key);
}
END_TEST

/* tests allocation of 1M page and writing to it and also the standard
 * data flow and call sequence */
static void
t2(wsrep_encrypt_cb_t cb, void* app_ctx, const gcache::Page::EncKey& key)
{
    bool const enc(NULL != cb);
    log_test(2, enc);

    const char* const dir_name = "";
    ssize_t const bh_size = sizeof(BufferHeader);
    ssize_t const page_size = (1 << 20) + bh_size;
    ssize_t const keep_size = page_size*2; // ensure that a page does not go away on free()
    ssize_t const buf_size  = page_size/2 - 1024;
    ssize_t const alloc_size = Page::aligned_size(buf_size);
    ssize_t const payload_size = buf_size - bh_size;
    assert(alloc_size < page_size/2);

    SeqnoMapStub sm;
    gcache::PageStore ps(sm, dir_name, cb, app_ctx, keep_size, page_size,page_size/2,
                         PageStore::DEBUG, false);
    ps.set_enc_key(key);

    get_BH BH(ps, enc);
    mark_point();

    void* ptx;
    uint8_t* buf1 = static_cast<uint8_t*>(ps.malloc(buf_size, ptx));
    ck_assert(0 != buf1);
    ck_assert(0 != ptx);
    if (enc) { ck_assert(ptx != buf1); } else { ck_assert(ptx == buf1); }

    uint64_t const b1(gu::FastHash::digest<uint64_t>(ptr2BH(buf1), alloc_size));

    for (int i(0); i < payload_size; ++i) { static_cast<char*>(ptx)[i] = i; }
    uint64_t const p1(gu::FastHash::digest<uint64_t>(ptx, payload_size));
    mark_point();

    if (enc) ps.drop_plaintext(buf1);
    uint64_t const b2(gu::FastHash::digest<uint64_t>(ptr2BH(buf1), alloc_size));
    if (enc)
    {
        /* should not flush plaintext yet:
         * plaintext size limit set at page_size/2 - greater than currently
         * allocated size  */
        ck_assert(b1 == b2);
    }
    else
    {
        /* should write directly to mmapped buffer */
        ck_assert(b1 != b2);
    }

    uint8_t* buf2 = static_cast<uint8_t*>(ps.malloc(buf_size, ptx));
    ck_assert(0 != buf2);
    ck_assert(0 != ptx);
    if (enc) { ck_assert(ptx != buf2); } else { ck_assert(ptx == buf2); }

    uint64_t const b3(gu::FastHash::digest<uint64_t>(ptr2BH(buf2), alloc_size));

    for (int i(0); i < payload_size; ++i) { static_cast<char*>(ptx)[i] = i + 1; }
    uint64_t const p2(gu::FastHash::digest<uint64_t>(ptx, payload_size));
    mark_point();

    if (enc) ps.drop_plaintext(buf2);
    uint64_t const b4(gu::FastHash::digest<uint64_t>(ptr2BH(buf2), alloc_size));
    ck_assert_msg(b3 != b4, "this time ptx should have been flushed");

    /* slave queue */

    const void* ptc(enc ? ps.get_plaintext(buf1) : buf1);
    uint64_t const p3(gu::FastHash::digest<uint64_t>(ptc, payload_size));
    ck_assert(p1 == p3);

    BufferHeader* const bh1(BH(buf1));
    ps_free(ps, bh1, buf1); /* ptx should be flushed, and buf1 discarded
                             * but the page stays and buf1 is still accessible */
    uint64_t const b5(gu::FastHash::digest<uint64_t>(ptr2BH(buf1), alloc_size));
    ck_assert(b5 != b2);
    ck_assert_msg(0 != ps.total_pages(), "1");
    // ps.discard (bh1, buf1); discard happens only for ordered buffers
    ck_assert_msg(0 != ps.total_pages(), "2");

    ptc = enc ? ps.get_plaintext(buf2) : buf2;
    uint64_t const p4(gu::FastHash::digest<uint64_t>(ptc, payload_size));
    ck_assert(p2 == p4);

    BufferHeader* const bh2(enc ? ps.get_BH(buf2, true) : ptr2BH(buf2));
    bh2->seqno_g = 1;
    ps.seqno_assign(bh2, 1);
    ps_free(ps, bh2, buf2); /* BH should be marked released */
    uint64_t const b6(gu::FastHash::digest<uint64_t>(ptr2BH(buf2), alloc_size)); 
    ck_assert_msg(b6 != b4, "b6 == b4, enc=%d", enc);
    ps.wait_page_discard ();
}

START_TEST(test2)
{
    t2(NULL, NULL, Key);
    t2(gcache_test_encrypt_cb, NULL, Key);
}
END_TEST

// checks that all page size is efficiently used
static void
t3(wsrep_encrypt_cb_t cb, void* app_ctx, const gcache::Page::EncKey& key)
{
    bool const enc(NULL != cb);
    log_test(3, enc);

    const char* const dir_name = "";
    ssize_t const keep_size = 1;
    ssize_t const page_overhead(gcache::Page::meta_size(BH_size(0)));
    ssize_t const page_size = 1024 + page_overhead;

    SeqnoMapStub sm;
    gcache::PageStore ps(sm, dir_name, cb, app_ctx, keep_size, page_size, page_size,
                         PageStore::DEBUG, true);
    ps.set_enc_key(key);

    get_BH BH(ps, enc);

    mark_point();

    ssize_t ptr_size = ((page_size - page_overhead) / 2);
    /* exactly half of the payload */
    assert(ptr_size == gcache::Page::aligned_size(ptr_size));

    void* ptx;
    void* ptr1 = ps.malloc(ptr_size, ptx);
    ck_assert(0 != ptr1);

    void* ptr2 = ps.malloc(ptr_size, ptx);
    ck_assert(0 != ptr2);

    ck_assert_msg(ps.count() == 1, "ps.count() = %zd, expected 1",
                  ps.count());

    // check that ptr2 is adjacent to ptr1
    void* tmp = static_cast<uint8_t*>(ptr1) + ptr_size;

    ck_assert_msg(tmp == ptr2, "tmp = %p, ptr2 = %p", tmp, ptr2);

    BufferHeader* const bh2(BH(ptr2));
    ps_free(ps, bh2, ptr2);
    BufferHeader* const bh1(BH(ptr1));
    ps_free(ps, bh1, ptr1);
    mark_point();
}

START_TEST(test3)
{
    t3(NULL, NULL, Key);
    t3(gcache_test_encrypt_cb, NULL, Key);
}
END_TEST

static void
t4(wsrep_encrypt_cb_t cb, void* app_ctx, const gcache::Page::EncKey& key)
{
    bool const enc(NULL != cb);
    log_test(4, enc);

    const char* const dir_name = "";
    ssize_t const page_size = 1024;
    ssize_t const keep_pages = 3;
    ssize_t const keep_size = keep_pages * page_size;
    ssize_t const alloc_size = page_size - gcache::Page::meta_size(BH_size(0));
    size_t expect;

    SeqnoMapStub sm;
    gcache::PageStore ps(sm, dir_name, cb, app_ctx, keep_size, page_size, page_size,
                         PageStore::DEBUG, false);
    ck_assert(ps.count() == 0);
    ck_assert(ps.total_pages() == 0);

    get_BH BH(ps, enc);

    ps.set_enc_key(key); /* key change should allocate new page */
    ck_assert(ps.count() == 1);
    ck_assert(ps.total_pages() == 1);

    void* ptx1;
    void* ptr1(ps.malloc(alloc_size, ptx1));
    ck_assert(NULL != ptx1);
    ck_assert(NULL != ptr1);
    expect = 1;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    void* ptx2;
    void* ptr2(ps.malloc(alloc_size, ptx2));
    ck_assert(NULL != ptx2);
    ck_assert(NULL != ptr2);
    expect = 2;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    void* ptx3;
    void* ptr3(ps.malloc(alloc_size, ptx3));
    ck_assert(NULL != ptx3);
    ck_assert(NULL != ptr3);
    expect = 3;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    void* ptx4;
    void* ptr4(ps.malloc(alloc_size, ptx4));
    ck_assert(NULL != ptx4);
    ck_assert(NULL != ptr4);
    expect = 4;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr1), ptr1);
    expect = keep_pages;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr2), ptr2);
    expect = keep_pages;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    void* ptx5;
    void* ptr5(ps.malloc(alloc_size, ptx5));
    ck_assert(NULL != ptx5);
    ck_assert(NULL != ptr5);
    expect = 3;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr5), ptr5);
    expect = 3;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr4), ptr4);
    expect = 3;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    void* ptx6;
    void* ptr6(ps.malloc(alloc_size, ptx6));
    ck_assert(NULL != ptx6);
    ck_assert(NULL != ptr6);
    expect = 4; // page 3 is still locked
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr6), ptr6);
    expect = 4; // page 3 is still locked
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    void* ptx7;
    void* ptr7(ps.malloc(alloc_size, ptx7));
    ck_assert(NULL != ptx7);
    ck_assert(NULL != ptr7);
    expect = 5;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr7), ptr7);
    expect = 5;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ps_free(ps, BH(ptr3), ptr3);
    expect = keep_pages;
    ck_assert_msg(ps.total_pages() == expect,
                  "Expected total_pages() = %zu, got %zu",
                  expect, ps.total_pages());

    ck_assert(ps.count() == 7);
}

START_TEST(test4) // check that pages linger correctly and get deleted as they
{                 // should when keep_size is exceeded
    t4(NULL, NULL, Key);
    t4(gcache_test_encrypt_cb, NULL, Key);
}
END_TEST

static void
test_caching_fill_page(gcache::PageStore&  ps,
                       std::vector<void*>& buf,
                       size_t const        expected_page_count)
{
    for (int i(0); i < 3; i++)
    {
        void* ptr; // plaintext buffer
        buf.push_back(ps.malloc(gcache::Limits::MIN_SIZE, ptr));
        ck_assert(nullptr != ptr);
        ck_assert(SEQNO_NONE == ptr2BH(ptr)->seqno_g);
        ck_assert_msg(ps.count() == expected_page_count,
                      "buf = %zu, ps.count() = %zu (expected %zu)",
                      buf.size()-1, ps.count(), expected_page_count);
    }
}

static void
caching_test(wsrep_encrypt_cb_t cb, void* app_ctx, const gcache::Page::EncKey& key)
{
    bool const enc(NULL != cb);
    log_test(5, enc);

    log_info << "\n#\n# test_caching\n#";
    const char* const dir_name = "";
    size_t const bh_size = sizeof(gcache::BufferHeader);
    size_t const page_size = (8 + bh_size)*3 // fits 3 buffers <= 8 bytes
                              + gcache::Page::meta_size(BH_size(0));
    size_t const keep_size = 2*page_size;     // keep at least 2 pages

    std::vector<void*> buf;

    SeqnoMapStub sm;
    gcache::PageStore ps(sm, dir_name, cb, app_ctx, keep_size, page_size, page_size,
                         PageStore::DEBUG, false);
    get_BH const BH(ps, enc);
    if (enc)
    {
        ps.set_enc_key(key); /* key change should allocate new page */
    }

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
    ps_free(ps, BH(buf[0]), buf[0]);
    ps_free(ps, BH(buf[1]), buf[1]);
    ps_free(ps, BH(buf[2]), buf[2]);
    ps.wait_page_discard(); // one page should go
    ck_assert_msg(5 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ps.wait_page_discard();
    ck_assert(sm.low_limit() == 0);
    ck_assert(sm.seqno_discarded() == 0);

    ps_free(ps, BH(buf[3]), buf[3]);
    ps.seqno_assign(BH(buf[4]), 1); ps_free(ps, BH(buf[4]), buf[4]);
    ps_free(ps, BH(buf[5]), buf[5]);
    ps.wait_page_discard(); // one page should go
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 1);

    ps_free(ps, BH(buf[6]), buf[6]);
    ps.seqno_assign(BH(buf[7]), 4); // this should pin the page
    ps_free(ps, BH(buf[8]), buf[8]);
    ps.wait_page_discard();
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 1);

    ps_free(ps, BH(buf[9]), buf[9]);
    ps.seqno_assign(BH(buf[11]), 2);
    ps.seqno_assign(BH(buf[10]), 3); ps_free(ps, BH(buf[10]), buf[10]);
    ps.wait_page_discard();
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());

    ps_free(ps, BH(buf[11]), buf[11]);
    ps.wait_page_discard();
    ck_assert_msg(4 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());

    ps_free(ps, BH(buf[7]), buf[7]);
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
    ps_free(ps, BH(buf[12]), buf[12]);
    ps.seqno_assign(BH(buf[13]), 5); ps_free(ps, BH(buf[13]), buf[13]);
    ps.seqno_assign(BH(buf[14]), 6); ps_free(ps, BH(buf[14]), buf[14]);
    ps.wait_page_discard(); // all pages should stay
    ck_assert_msg(2 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 4);

    ps.seqno_assign(BH(buf[15]), 7); ps_free(ps, BH(buf[15]), buf[15]);
    ps.seqno_assign(BH(buf[16]), 8); ps_free(ps, BH(buf[16]), buf[16]);
    ps_free(ps, BH(buf[17]), buf[17]);
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
    void* pt;
    buf.push_back(ps.malloc(gcache::Limits::MIN_SIZE, pt));
    ck_assert(old_page_count + 1 == ps.count()); // new page was allocated
    ck_assert_msg(2 == ps.total_pages(),
                  "keep_pages/total_pages = %zu/%zu, "
                  "keep_size/total_size = %zu/%zu",
                  ps.keep_page(), ps.total_pages(),
                  ps.keep_size(), ps.total_size());
    ck_assert(sm.low_limit() == 6);

    for (size_t i(18); i < buf.size(); i++)
    {
        ps_free(ps, BH(buf[i]), buf[i]);
    }

    mark_point();
}

START_TEST(test_caching) // test that caching in pages work
{
    caching_test(NULL, NULL, Key);
    caching_test(gcache_test_encrypt_cb, NULL, Key);
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
    tcase_add_test(tc, test4);
    tcase_add_test(tc, test_caching);
    suite_add_tcase(s, tc);

    return s;
}
