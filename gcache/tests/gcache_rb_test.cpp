/*
 * Copyright (C) 2011-2018 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GCACHE_RB_UNIT_TEST

#include "gcache_rb_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_rb_test.hpp"

#include <gu_logger.hpp>
#include <gu_throw.hpp>

using namespace gcache;

static gu::UUID    const GID(NULL, 0);
static std::string const RB_NAME("rb_test");
static size_t      const BH_SIZE(sizeof(gcache::BufferHeader));

typedef MemOps::size_type size_type;

static size_type ALLOC_SIZE(size_type s)
{
    return MemOps::align_size(s + BH_SIZE);
}

START_TEST(test1)
{
    ::unlink(RB_NAME.c_str());

    size_t const rb_size(ALLOC_SIZE(2) * 2);

    std::map<int64_t, const void*> s2p;
    gu::UUID   gid(GID);
    RingBuffer rb(RB_NAME, rb_size, s2p, gid, 0, false);

    fail_if (rb.size() != rb_size, "Expected %zd, got %zd", rb_size, rb.size());

    if (gid != GID)
    {
        std::ostringstream os;
        os << "Expected GID: " << GID << ", got: " << gid;
        fail(os.str().c_str());
    }

    void* buf1 = rb.malloc (MemOps::align_size(rb_size/2 + 1));
    fail_if (NULL != buf1); // > 1/2 size

    buf1 = rb.malloc (ALLOC_SIZE(1));
    fail_if (NULL == buf1);

    BufferHeader* bh1(ptr2BH(buf1));
    fail_if (bh1->seqno_g != SEQNO_NONE);
    fail_if (BH_is_released(bh1));

    void* buf2 = rb.malloc (ALLOC_SIZE(2));
    fail_if (NULL == buf2);
    fail_if (BH_is_released(bh1));

    BufferHeader* bh2(ptr2BH(buf2));
    fail_if (bh2->seqno_g != SEQNO_NONE);
    fail_if (BH_is_released(bh2));

    void* tmp = rb.realloc (buf1, ALLOC_SIZE(2));
    // anything <= MemOps::ALIGNMENT should fit into original buffer
    fail_if(tmp != buf1 && MemOps::ALIGNMENT > 1);

    tmp = rb.realloc (buf1, ALLOC_SIZE(MemOps::ALIGNMENT + 1));
    // should require new buffer for which there's no space
    fail_if (bh2->seqno_g != SEQNO_NONE);
    fail_if (NULL != tmp);

    BH_release(bh2);
    rb.free (bh2);

    tmp = rb.realloc (buf1, ALLOC_SIZE(3));
    if (MemOps::ALIGNMENT > 2)
    {
        fail_if (NULL == tmp);
        fail_if (buf1 != tmp);
    }
    else
    {
        fail_if (NULL != tmp);
    }

    BH_release(bh1);
    rb.free (bh1);
    fail_if (!BH_is_released(bh1));

    buf1 = rb.malloc(ALLOC_SIZE(1));
    fail_if (NULL == buf1);

    tmp = rb.realloc (buf1, ALLOC_SIZE(2));
    fail_if (NULL == tmp);
    fail_if (tmp != buf1);

    buf2 = rb.malloc (ALLOC_SIZE(1));
    fail_if (NULL == buf2);

    tmp = rb.realloc (buf2, ALLOC_SIZE(2));
    fail_if (NULL == tmp);
    fail_if (tmp != buf2);

    tmp = rb.malloc (ALLOC_SIZE(1));
    fail_if (NULL != tmp);

    BH_release(ptr2BH(buf1));
    rb.free(ptr2BH(buf1));

    BH_release(ptr2BH(buf2));
    rb.free(ptr2BH(buf2));

    tmp = rb.malloc (ALLOC_SIZE(2));
    fail_if (NULL == tmp);

    mark_point();
}
END_TEST

START_TEST(recovery)
{
    struct msg
    {
        char    msg;
        seqno_t g;
        seqno_t d;

        size_t size() const { return sizeof(msg); }
    };

#define MAX_MSGS 10
    struct msg msgs[MAX_MSGS] =
    {
        { '0',         1,         0 },
        { '1',         2,         0 },
        { '2',         4,         1 },
        { '3', SEQNO_ILL, SEQNO_ILL },
        { '4',         3,         1 },
        { '5', SEQNO_ILL, SEQNO_ILL },
        { '6',         5, SEQNO_ILL },
        { '7', SEQNO_ILL, SEQNO_ILL },
        { '8',         6,         4 },
        { '9',         7,         4 }
    };

    size_type const msg_size(ALLOC_SIZE(sizeof(reinterpret_cast<msg*>(0)->msg)));

    struct rb_ctx
    {
        size_t const size;
        seqno2ptr_t  s2p;
        gu::UUID     gid;
        RingBuffer   rb;

        rb_ctx(size_t s, bool recover = true) :
            size(s), s2p(), gid(GID), rb(RB_NAME, size, s2p, gid, 0, recover) {}

        void seqno_assign (seqno2ptr_t& s2p, void* const ptr,
                           seqno_t const g, seqno_t const d)
        {
            const std::pair<seqno2ptr_iter_t, bool>& res
                (s2p.insert(seqno2ptr_pair_t(g, ptr)));

            if (false == res.second)
            {
                gu_throw_fatal <<"Attempt to reuse the same seqno: " << g
                               <<". New ptr = " << ptr << ", previous ptr = "
                               << res.first->second;
            }

            BufferHeader* bh(ptr2BH(ptr));
            bh->seqno_g = g;
            if (d < 0) bh->flags |= BUFFER_SKIPPED;
        }

        void* add_msg(struct msg& m)
        {
            void* ret(rb.malloc(ALLOC_SIZE(m.size())));

            if (ret)
            {
                ::memcpy(ret, &m.msg, m.size());

                if (m.g > 0) seqno_assign(s2p, ret, m.g, m.d);

                BH_release(ptr2BH(ret));
                rb.free(ptr2BH(ret));
            }

            return ret;
        }

        void print_map()
        {
            std::ostringstream os;
            os << "S2P map:\n";
            for (seqno2ptr_t::iterator i = s2p.begin(); i != s2p.end(); ++i)
            {
                log_info << "\tseqno: " << i->first << ", msg: "
                         << reinterpret_cast<const char*>(i->second) << "\n";
            }

            log_info << os.str();
        }
    };

    seqno_t seqno_min, seqno_max;
    size_t const rb_5size(msg_size*5);

    {
        rb_ctx ctx(rb_5size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(!ctx.s2p.empty());

        void* m(ctx.add_msg(msgs[0]));
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[0].g)->second != m);

        m = ctx.add_msg(msgs[1]);
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[1].g)->second != m);

        m = ctx.add_msg(msgs[2]);
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[2].g)->second != m);

        m = ctx.add_msg(msgs[3]);
        fail_if (NULL == m);
        fail_if (msgs[3].g > 0);
        fail_if (ctx.s2p.find(msgs[3].g) != ctx.s2p.end());

        seqno_min = ctx.s2p.begin()->first;
        seqno_max = ctx.s2p.rbegin()->first;
    }

    /* What we have now is |111222444***|----| */
    /* Reopening of the file should:
     * 1) discard messages 1, 2 since there is a hole at 3. Only 4 should remain
     * 2) trim the trailing unordered message
     */
    {
        rb_ctx ctx(rb_5size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 1);
        fail_if(ctx.s2p.begin()->first == seqno_min);
        fail_if(ctx.s2p.begin()->first != seqno_max);

        void* m(ctx.add_msg(msgs[4]));
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[4].g)->second != m);

        m = ctx.add_msg(msgs[5]);
        fail_if (NULL == m);
        fail_if (msgs[5].g > 0);
        fail_if (ctx.s2p.find(msgs[5].g) != ctx.s2p.end());

        m = ctx.add_msg(msgs[6]);
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[6].g)->second != m);
        // here we should have rollover
        fail_if (ptr2BH(m) != BH_cast(ctx.rb.start()));

        seqno_min = ctx.s2p.begin()->first;
        seqno_max = ctx.s2p.rbegin()->first;
    }

    /* What we have now is |555|---|444333***| */
    /* Reopening of the file should:
     * 1) discard discard unordered message at the end
     * 2) continuous seqno interval should be now 3,4,5
     */
    {
        rb_ctx ctx0(rb_5size);

        fail_if (ctx0.rb.size() != ctx0.size,
                 "Expected %zd, got %zd", ctx0.size, ctx0.rb.size());

        if (ctx0.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx0.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx0.s2p.empty());
        fail_if(ctx0.s2p.size() != 3);
        fail_if(ctx0.s2p.begin()->first  != seqno_min);
        fail_if(ctx0.s2p.rbegin()->first != seqno_max);

        /* now try to open unclosed file. Results should be the same */
        rb_ctx ctx(rb_5size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 3);
        fail_if(ctx.s2p.begin()->first  != seqno_min);
        fail_if(ctx.s2p.rbegin()->first != seqno_max);

        seqno_min = ctx.s2p.begin()->first;
        seqno_max = ctx.s2p.rbegin()->first;
    }

    size_t const rb_3size(msg_size*3);

    /* now try to truncate the buffer. Only seqno 4,5 should remain */
    /* |555---444| */
    {
        rb_ctx ctx(rb_3size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 2);
        fail_if(ctx.s2p.begin()->first  == seqno_min);
        fail_if(ctx.s2p.rbegin()->first != seqno_max);

        void* m(ctx.add_msg(msgs[8]));
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[8].g)->second != m);

        m = ctx.add_msg(msgs[9]);
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[9].g)->second != m);

        m = ctx.add_msg(msgs[7]);
        fail_if (NULL == m);
        fail_if (msgs[7].g > 0);
        fail_if (ctx.s2p.find(msgs[7].g) != ctx.s2p.end());
        // here we should have rollover
        fail_if (ptr2BH(m) != BH_cast(ctx.rb.start()));

        seqno_min = ctx.s2p.begin()->first;
        seqno_max = ctx.s2p.rbegin()->first;
    }

    /* what we should have now is |***---777| - only one segment, at the end */
    {
        /* first open this with known offset */
        rb_ctx ctx0(rb_3size);

        fail_if (ctx0.rb.size() != ctx0.size,
                 "Expected %zd, got %zd", ctx0.size, ctx0.rb.size());

        if (ctx0.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx0.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx0.s2p.empty());
        fail_if(ctx0.s2p.size() != 1);
        fail_if(ctx0.s2p.begin()->first  != seqno_max);
        fail_if(ctx0.s2p.rbegin()->first != seqno_max);

        /* now try to open unclosed file. Results should be the same */
        rb_ctx ctx(rb_3size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 1);
        fail_if(ctx.s2p.begin()->first  != seqno_max);
        fail_if(ctx.s2p.rbegin()->first != seqno_max);

        fail_if(seqno_max < 1);
        fail_if(seqno_min != seqno_max);
    }

    ::unlink(RB_NAME.c_str());

    /* test for singe segment in the middle */
    void* third_buffer(NULL);
    {
        rb_ctx ctx(rb_3size, false);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(!ctx.s2p.empty());

        void* m(ctx.add_msg(msgs[3]));
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[3].g) != ctx.s2p.end());

        m = ctx.add_msg(msgs[4]);
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[4].g)->second != m);

        m = ctx.add_msg(msgs[5]);
        fail_if (NULL == m);
        fail_if (ctx.s2p.find(msgs[5].g) != ctx.s2p.end());
        third_buffer = m;

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 1);
        seqno_min = ctx.s2p.begin()->first;
        seqno_max = ctx.s2p.rbegin()->first;
        fail_if(seqno_min != seqno_max);
    }

    /* now the situation should be |***444***| - only one segment, in the middle,
     * reopen the file with a known position */
    {
        rb_ctx ctx(rb_3size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 1);
        fail_if(seqno_min != ctx.s2p.begin()->first);
        fail_if(seqno_max != ctx.s2p.rbegin()->first);
        fail_if(seqno_min != seqno_max);
    }

    /* now the situation should be |---444---| - only one segment, in the middle,
     * reopen the file a second time - to trigger a rollover bug */
    {
        rb_ctx ctx(rb_3size);

        fail_if (ctx.rb.size() != ctx.size,
                 "Expected %zd, got %zd", ctx.size, ctx.rb.size());

        if (ctx.gid != GID)
        {
            std::ostringstream os;
            os << "Expected GID: " << GID << ", got: " << ctx.gid;
            fail(os.str().c_str());
        }

        fail_if(ctx.s2p.empty());
        fail_if(ctx.s2p.size() != 1);
        fail_if(seqno_min != ctx.s2p.begin()->first);
        fail_if(seqno_max != ctx.s2p.rbegin()->first);
        fail_if(seqno_min != seqno_max);

        // must be allocated right after the recovered buffer
        void* m(ctx.add_msg(msgs[3]));
        fail_if (NULL == m);
        fail_if (third_buffer != m);
    }

    ::unlink(RB_NAME.c_str());
}
END_TEST


Suite* gcache_rb_suite()
{
    Suite* ts = suite_create("gcache::RbStore");
    TCase* tc = tcase_create("test");

    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, test1);
    suite_add_tcase(ts, tc);

    tc = tcase_create("recovery");

    tcase_set_timeout(tc, 60);
    tcase_add_test(tc, recovery);
    suite_add_tcase(ts, tc);

    return ts;
}
