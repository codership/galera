//
// Copyright (C) 2011-2020 Codership Oy <info@codership.com>
//

#include "gu_vlq.hpp"
#include "gu_vlq_test.hpp"
#include "gu_logger.hpp"
#include "gu_inttypes.hpp"

#include <stdint.h>
#include <cstdlib>
#include <vector>
#include <limits>

static struct valval
{
    const unsigned long long val;
    const size_t             size;
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

// http://www.cplusplus.com/faq/sequences/arrays/sizeof-array/
template <typename T, size_t N>
inline
size_t SizeOfArray( const T(&)[ N ] )
{
    return N;
}

START_TEST(test_uleb128_size)
{
    for (size_t i(0); i < SizeOfArray(valarr); ++i)
    {
        size_t size(gu::uleb128_size(valarr[i].val));
        ck_assert_msg(size == valarr[i].size,
                      "got size %zu, expected %zu for value 0x%llx",
                      size, valarr[i].size, valarr[i].val);
    }
}
END_TEST


START_TEST(test_uleb128_encode)
{
    std::vector<gu::byte_t> buf;
    for (size_t i(0); i < SizeOfArray(valarr); ++i)
    {
        buf.resize(valarr[i].size);
        size_t offset(gu::uleb128_encode(valarr[i].val, &buf[0],
                                         buf.size(), 0));
        ck_assert_msg(offset == valarr[i].size,
                      "got offset %zu, expected %zu for value 0x%llx",
                      offset, valarr[i].size, valarr[i].val);
    }
}
END_TEST


START_TEST(test_uleb128_decode)
{
    std::vector<gu::byte_t> buf;
    for (size_t i(0); i < SizeOfArray(valarr); ++i)
    {
        buf.resize(valarr[i].size);
        size_t offset(gu::uleb128_encode(valarr[i].val, &buf[0],
                                         buf.size(), 0));
        unsigned long long val;
        try
        {
            offset = gu::uleb128_decode(&buf[0], buf.size(), 0, val);
            ck_assert_msg(offset == valarr[i].size,
                          "got offset %zu, expected %zu for value 0x%llx",
                          offset, valarr[i].size, valarr[i].val);
            ck_assert_msg(val == valarr[i].val,
                          "got value 0x%llx, expected 0x%llx",
                          val, valarr[i].val);
        }
        catch (gu::Exception& e)
        {
            ck_abort_msg("Exception in round %zu for encoding of size %zu: %s",
                         i, valarr[i].size, e.what());
        }
    }
}
END_TEST


START_TEST(test_uleb128_misc)
{
    std::vector<gu::byte_t> buf(10);

    // check uint8_t whole range
    for (size_t i(0); i <= std::numeric_limits<uint8_t>::max(); ++i)
    {
        (void)gu::uleb128_encode<uint8_t>(static_cast<uint8_t>(i), &buf[0],
                                          buf.size(), 0);
        uint8_t val;
        (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val);
        if (i != val) ck_abort_msg("0x%zx != 0x%x", i, val);
    }

    // check uint16_t whole range
    for (size_t i(0); i <= std::numeric_limits<uint16_t>::max(); ++i)
    {
        (void)gu::uleb128_encode<uint16_t>(static_cast<uint16_t>(i),
                                           &buf[0], buf.size(), 0);
        uint16_t val;
        (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val);
        if (i != val) ck_abort_msg("0x%zx != 0x%x", i, val);
    }

    // check uint32_t: 0 -> 1^20
    for (size_t i(0); i < (1 << 20); ++i)
    {
        (void)gu::uleb128_encode<uint32_t>(static_cast<uint32_t>(i),
                                           &buf[0], buf.size(), 0);
        uint32_t val;
        (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val);
        if (i != val) ck_abort_msg("0x%zx != 0x%x", i, val);
    }

    // check uin32_t: max - 1^20 -> max
    for (uint64_t i(std::numeric_limits<uint32_t>::max() - (1 << 20));
         i <= std::numeric_limits<uint32_t>::max(); ++i)
    {
        (void)gu::uleb128_encode<uint32_t>(static_cast<uint32_t>(i),
                                           &buf[0], buf.size(), 0);
        uint32_t val;
        (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val);
        if (i != val) ck_abort_msg("0x%zx != 0x%x", i, val);
    }


    // uint64_t is tested for representation byte boundaries earlier,
    // run test just for random values
    for (size_t i(0); i < (1 << 16); ++i)
    {
        unsigned long long val(static_cast<uint64_t>(rand())
                               * static_cast<uint64_t>(rand()));
        (void)gu::uleb128_encode(val, &buf[0], buf.size(), 0);
        unsigned long long val2;
        (void)gu::uleb128_decode(&buf[0], buf.size(), 0, val2);
        if (val != val2) ck_abort_msg("0x%llx != 0x%llx", val, val2);
    }

    {
        // check that exception is thrown if target type is not
        // wide enough

        // uint8_t
        uint64_t val(static_cast<uint64_t>(std::numeric_limits<uint8_t>::max())
                     + 1);
        buf.resize(gu::uleb128_size(val));
        (void)gu::uleb128_encode(val, &buf[0], buf.size(), 0);
        try
        {
            uint8_t cval;
            (void)gu::uleb128_decode(&buf[0], buf.size(), 0, cval);
            ck_abort_msg("exception was not thrown");
        }
        catch (gu::Exception& e)
        {
            log_info << "expected exception: " << e.what();
        }

        // uint16_t
        val = static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1;
        buf.resize(gu::uleb128_size(val));
        (void)gu::uleb128_encode(val, &buf[0], buf.size(), 0);
        try
        {
            uint16_t cval;
            (void)gu::uleb128_decode(&buf[0], buf.size(), 0, cval);
            ck_abort_msg("exception was not thrown");
        }
        catch (gu::Exception& e)
        {
            log_info << "expected exception: " << e.what();
        }

        // uint32_t
        val = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
        buf.resize(gu::uleb128_size(val));
        (void)gu::uleb128_encode(val, &buf[0], buf.size(), 0);
        try
        {
            uint32_t cval;
            (void)gu::uleb128_decode(&buf[0], buf.size(), 0, cval);
            ck_abort_msg("exception was not thrown");
        }
        catch (gu::Exception& e)
        {
            log_info << "expected exception: " << e.what();
        }

        // check that exception is thrown if terminating byte is missing
        buf.resize(buf.size() - 1);
        try
        {
            uint64_t cval;
            (void)gu::uleb128_decode(&buf[0], buf.size(), 0, cval);
            ck_abort_msg("exception was not thrown");
        }
        catch (gu::Exception& e)
        {
            log_info << "expected exception: " << e.what();
        }


        // finally check the representation that cannot be stored with
        // uint64_t

        gu::byte_t b[] = {0x80, 0x80, 0x80, 0x80,
                          0x80, 0x80, 0x80, 0x80,
                          0x80, // <--- up here 9 * 7 = 63 bits
                          0x02}; // <--- requires two additional bits
        try
        {
            uint64_t cval;
            (void)gu::uleb128_decode(b, SizeOfArray(b), 0, cval);
            ck_abort_msg("exception was not thrown");
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
