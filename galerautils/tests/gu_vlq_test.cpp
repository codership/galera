//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

#include "gu_vlq.hpp"
#include "gu_vlq_test.hpp"

#include <cstdlib>
#include <vector>

static struct valarr
{
    unsigned long long val;
    size_t             size;
} valarr[] =
{
    {0x00                 , 1},
    {0x01                 , 1},
    {0x7fULL              , 1},
    {0x80ULL              , 2},
    {0x3fffULL            , 2},
    {0x4000ULL            , 3},
    {0x1fffffULL          , 3},
    {0x200000ULL          , 4},
    {0x0fffffffULL        , 4},
    {0x10000000ULL        , 5},
    {0x07ffffffffULL      , 5},
    {0x0800000000ULL      , 6},
    {0x03ffffffffffULL    , 6},
    {0x040000000000ULL    , 7},
    {0x01ffffffffffffULL  , 7},
    {0x02000000000000ULL  , 8},
    {0x00ffffffffffffffULL, 8},
    {0x0100000000000000ULL, 9},
    {0x7fffffffffffffffULL, 9},
    {0x8000000000000000ULL, 10},
    {0xffffffffffffffffULL, 10}
};


START_TEST(test_uleb128_size)
{
    for (size_t i(0); i < sizeof(valarr)/sizeof(struct valarr); ++i)
    {
        size_t size(gu::uleb128_size(valarr[i].val));
        fail_unless(size == valarr[i].size,
                    "got size %z, expected %z for value 0x%llx",
                    size, valarr[i].size, valarr[i].val);
    }
}
END_TEST


START_TEST(test_uleb128_encode)
{
    std::vector<gu::byte_t> buf;
    for (size_t i(0); i < sizeof(valarr)/sizeof(struct valarr); ++i)
    {
        buf.resize(valarr[i].size);
        size_t offset(gu::uleb128_encode(valarr[i].val, &buf[0],
                                         buf.size(), 0));
        fail_unless(offset == valarr[i].size,
                    "got offset %zu, expected %zu for value 0x%llx",
                    offset, valarr[i].size, valarr[i].val);
    }
}
END_TEST


START_TEST(test_uleb128_decode)
{
    std::vector<gu::byte_t> buf;
    for (size_t i(0); i < sizeof(valarr)/sizeof(struct valarr); ++i)
    {
        buf.resize(valarr[i].size);
        size_t offset(gu::uleb128_encode(valarr[i].val, &buf[0],
                                         buf.size(), 0));
        unsigned long long val;
        offset = gu::uleb128_decode(&buf[0], buf.size(), 0, val);
        fail_unless(offset == valarr[i].size,
                    "got offset %zu, expected %zu for value 0x%llx",
                    offset, valarr[i].size, valarr[i].val);
        fail_unless(val == valarr[i].val,
                    "got value 0x%llx, expected 0x%llx",
                    val, valarr[i].val);
    }
}
END_TEST


START_TEST(test_uleb128_misc)
{
    std::vector<gu::byte_t> buf(10);
    for (size_t i(0); i < (1 << 16); ++i)
    {
        unsigned long long val(static_cast<unsigned long long>(rand())
                               * static_cast<unsigned long long>(rand()));
        (void)gu::uleb128_encode(val, &buf[0], buf.size(), 0);
        unsigned long long val2;
        (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val2);
        if (val != val2) fail("0x%llx != 0x%llx", val, val2);
    }


    {
        unsigned long long val(0xefffff);
        buf.resize(gu::uleb128_size(val));
        (void)gu::uleb128_encode(val, &buf[0], buf.size(), 0);

        try
        {
            unsigned char cval;
            (void)gu::uleb128_decode(&buf[0], buf.size(), 0, cval);
            fail("exception was not thrown");
        }
        catch (gu::Exception& e)
        {
            log_info << "expected exception: " << e.what();
        }
        buf.resize(buf.size() - 1);
        try
        {
            unsigned long long val2;
            (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val2);
        }
        catch (gu::Exception& e)
        {
            log_info << "expected exception: " << e.what();
        }

    }




}
END_TEST


Suite* gu_vlq_suite()
{
    Suite* s(suite_create("gu::vlq"));
    TCase* tc;

    tc = tcase_create("test_uleb128_size");
    tcase_add_test(tc, test_uleb128_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_uleb128_encode");
    tcase_add_test(tc, test_uleb128_encode);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_uleb128_decode");
    tcase_add_test(tc, test_uleb128_decode);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_uleb128_misc");
    tcase_add_test(tc, test_uleb128_misc);
    suite_add_tcase(s, tc);

    return s;
}
