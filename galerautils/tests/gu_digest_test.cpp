// Copyright (C) 2012-2020 Codership Oy <info@codership.com>

/*
 * This unit test is mostly to check that Galera hash definitions didn't change:
 * correctness of hash algorithms definitions is checked in respective unit
 * tests.
 *
 * By convention checks are made against etalon byte arrays, so integers must be
 * converted to little-endian.
 *
 * $Id$
 */

#include "../src/gu_digest.hpp"

#include "gu_digest_test.hpp"

#include "../src/gu_hexdump.hpp"
#include "../src/gu_logger.hpp"
#include "../src/gu_inttypes.hpp"

/* checks equivalence of two buffers, returns true if check fails and logs
 * buffer contents. */
static bool
check (const void* const exp, const void* const got, ssize_t size)
{
    if (memcmp (exp, got, size))
    {
        log_info << "expected hash value:\n" << gu::Hexdump(exp, size)
                 << "\nfound:\n" << gu::Hexdump(got, size) << "\n";

        return true;
    }

    return false;
}


static const char test_msg[2048] = { 0, };

#define GU_HASH_TEST_LENGTH 43 /* some random prime */

static const uint8_t gu_hash128_check[16] = {
0xFA,0x2C,0x78,0x67,0x35,0x99,0xD9,0x84,0x73,0x41,0x3F,0xA5,0xEB,0x27,0x40,0x2F
};

static const uint8_t gu_hash64_check[8]  = {
0xFA,0x2C,0x78,0x67,0x35,0x99,0xD9,0x84
};

static const uint8_t gu_hash32_check[4]  = { 0xFA,0x2C,0x78,0x67 };

/* Tests partial hashing functions */
START_TEST (gu_hash_test)
{
    gu::Hash hash_one;

    hash_one.append(test_msg, GU_HASH_TEST_LENGTH);

    uint8_t res128_one[16];
    hash_one.gather<sizeof(res128_one)>(res128_one);
    ck_assert_msg(!check(gu_hash128_check, res128_one, sizeof(res128_one)),
                  "gu::Hash::gather() failed in single mode.");

    gu::Hash::digest(test_msg, GU_HASH_TEST_LENGTH, res128_one);
    ck_assert_msg(!check(gu_hash128_check, res128_one, sizeof(res128_one)),
                  "gu::Hash::digest() failed.");

    gu::Hash hash_multi;

    int off = 0;
    hash_multi.append(test_msg, 16);

    off += 16;
    hash_multi.append(test_msg + off, 15);

    off += 15;
    hash_multi.append(test_msg + off, 7);

    off += 7;
    hash_multi.append(test_msg + off, 5);

    off += 5;
    ck_assert(off == GU_HASH_TEST_LENGTH);

    uint8_t res128_multi[16];
    hash_multi.gather<sizeof(res128_multi)>(res128_multi);
    ck_assert_msg(!check(gu_hash128_check, res128_multi, sizeof(res128_multi)),
                  "gu::Hash::gather() failed in multi mode.");

    uint64_t res64;
    hash_multi.gather<sizeof(res64)>(&res64);
    uint64_t const res(gu_hash64(test_msg, GU_HASH_TEST_LENGTH));
    ck_assert_msg(res == res64, "got 0x%0" PRIx64 ", expected 0x%" PRIx64,
                  res64, res);
    res64 = gu_le64(res64);
    ck_assert_msg(!check(gu_hash64_check, &res64, sizeof(res64)),
                  "gu::Hash::gather<uint64_t>() failed.");

    uint32_t res32;
    hash_one(res32);
    ck_assert(gu_hash32(test_msg, GU_HASH_TEST_LENGTH) == res32);
    res32 = gu_le32(res32);
    ck_assert_msg(!check(gu_hash32_check, &res32, sizeof(res32)),
                  "gu::Hash::gather<uint32_t>() failed.");
}
END_TEST

static const uint8_t fast_hash128_check0   [16] = {
0xA9,0xCE,0x5A,0x56,0x0C,0x0B,0xF7,0xD6,0x63,0x4F,0x6F,0x81,0x0E,0x0B,0xF2,0x0A
};
static const uint8_t fast_hash128_check511 [16] = {
0xC6,0x7F,0x4C,0xE7,0x6F,0xE0,0xDA,0x14,0xCC,0x9F,0x21,0x76,0xAF,0xB5,0x12,0x1A
};
static const uint8_t fast_hash128_check512 [16] = {
0x38,0x8D,0x2B,0x90,0xC8,0x7F,0x11,0x53,0x3F,0xB4,0x32,0xC1,0xD7,0x2B,0x04,0x39
};
static const uint8_t fast_hash128_check2011[16] = {
0xB7,0xCE,0x75,0xC7,0xB4,0x31,0xBC,0xC8,0x95,0xB3,0x41,0xB8,0x5B,0x8E,0x77,0xF9
};

static const uint8_t fast_hash64_check0   [8] = {
    0x6C, 0x55, 0xB8, 0xA1, 0x02, 0xC6, 0x21, 0xCA
};
static const uint8_t fast_hash64_check15  [8] = {
    0x28, 0x49, 0xE8, 0x34, 0x7A, 0xAB, 0x49, 0x34
};
static const uint8_t fast_hash64_check16  [8] = {
    0x44, 0x40, 0x2C, 0x82, 0xD3, 0x8D, 0xAA, 0xFE
};
static const uint8_t fast_hash64_check511 [8] = {
    0xC6, 0x7F, 0x4C, 0xE7, 0x6F, 0xE0, 0xDA, 0x14
};
static const uint8_t fast_hash64_check512 [8] = {
    0x38, 0x8D, 0x2B, 0x90, 0xC8, 0x7F, 0x11, 0x53
};
static const uint8_t fast_hash64_check2011[8] = {
    0xB7, 0xCE, 0x75, 0xC7, 0xB4, 0x31, 0xBC, 0xC8
};

static const uint8_t fast_hash32_check0   [4] = { 0x0B, 0x7C, 0x3E, 0xAB };
static const uint8_t fast_hash32_check31  [4] = { 0x1E, 0xFF, 0x48, 0x38 };
static const uint8_t fast_hash32_check32  [4] = { 0x63, 0xC2, 0x53, 0x0D };
static const uint8_t fast_hash32_check511 [4] = { 0xC6, 0x7F, 0x4C, 0xE7 };
static const uint8_t fast_hash32_check512 [4] = { 0x38, 0x8D, 0x2B, 0x90 };
static const uint8_t fast_hash32_check2011[4] = { 0xB7, 0xCE, 0x75, 0xC7 };

/* Tests fast hash functions */
START_TEST (gu_fast_hash_test)
{
    uint8_t res128[16];

    gu::FastHash::digest (test_msg, 0,    res128);
    ck_assert(!check(fast_hash128_check0,    res128, sizeof(res128)));

    gu::FastHash::digest (test_msg, 511,  res128);
    ck_assert(!check(fast_hash128_check511,  res128, sizeof(res128)));

    gu::FastHash::digest (test_msg, 512,  res128);
    ck_assert(!check(fast_hash128_check512,  res128, sizeof(res128)));

    gu::FastHash::digest (test_msg, 2011, res128);
    ck_assert(!check(fast_hash128_check2011, res128, sizeof(res128)));

    uint64_t res64;

    res64 = gu::FastHash::digest<uint64_t>(test_msg, 0); res64 = gu_le64(res64);
    ck_assert(!check(fast_hash64_check0, &res64, sizeof(res64)));

    res64 = gu::FastHash::digest<uint64_t>(test_msg,15); res64 = gu_le64(res64);
    ck_assert(!check(fast_hash64_check15, &res64, sizeof(res64)));

    res64 = gu::FastHash::digest<uint64_t>(test_msg,16); res64 = gu_le64(res64);
    ck_assert(!check(fast_hash64_check16, &res64, sizeof(res64)));

    res64 = gu::FastHash::digest<uint64_t>(test_msg,511); res64 =gu_le64(res64);
    ck_assert(!check(fast_hash64_check511, &res64, sizeof(res64)));

    res64 = gu::FastHash::digest<uint64_t>(test_msg,512); res64 =gu_le64(res64);
    ck_assert(!check(fast_hash64_check512, &res64, sizeof(res64)));

    res64 = gu::FastHash::digest<uint64_t>(test_msg,2011);res64 =gu_le64(res64);
    ck_assert(!check(fast_hash64_check2011, &res64, sizeof(res64)));

    uint32_t res32;

    res32 = gu::FastHash::digest<uint32_t>(test_msg, 0); res32 = gu_le32(res32);
    ck_assert(!check(fast_hash32_check0, &res32, sizeof(res32)));

    res32 = gu::FastHash::digest<uint32_t>(test_msg,31); res32 = gu_le32(res32);
    ck_assert(!check(fast_hash32_check31, &res32, sizeof(res32)));

    res32 = gu::FastHash::digest<uint32_t>(test_msg,32); res32 = gu_le32(res32);
    ck_assert(!check(fast_hash32_check32, &res32, sizeof(res32)));

    res32 = gu::FastHash::digest<uint32_t>(test_msg,511); res32 =gu_le32(res32);
    ck_assert(!check(fast_hash32_check511, &res32, sizeof(res32)));

    res32 = gu::FastHash::digest<uint32_t>(test_msg,512); res32 =gu_le32(res32);
    ck_assert(!check(fast_hash32_check512, &res32, sizeof(res32)));

    res32 = gu::FastHash::digest<uint32_t>(test_msg,2011); res32=gu_le32(res32);
    ck_assert(!check(fast_hash32_check2011, &res32, sizeof(res32)));
}
END_TEST

#if SKIP_TABLE_FUNCTIONS

/* Tests table hash functions:
 * - for 64-bit platforms table hash should be identical to fast 64-bit hash,
 * - for 32-bit platforms table hash is different.
 */
#if GU_WORDSIZE == 64

START_TEST (gu_table_hash_test)
{
    size_t res;

    ck_assert(sizeof(res) > 8);

    res = gu_table_hash (test_msg, 0); res = gu_le64(res);
    ck_assert(!check(fast_hash64_check0, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 15); res = gu_le64(res);
    ck_assert(!check(fast_hash64_check15, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 16); res = gu_le64(res);
    ck_assert(!check(fast_hash64_check16, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 511); res = gu_le64(res);
    ck_assert(!check(fast_hash64_check511, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 512); res = gu_le64(res);
    ck_assert(!check(fast_hash64_check512, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 2011); res = gu_le64(res);
    ck_assert(!check(fast_hash64_check2011, &res, sizeof(res)));
}
END_TEST

#elif GU_WORDSIZE == 32

static const uint8_t table_hash32_check0   [4] = { 0x0B, 0x7C, 0x3E, 0xAB };
static const uint8_t table_hash32_check32  [4] = { 0x65, 0x16, 0x17, 0x42 };
static const uint8_t table_hash32_check2011[4] = { 0xF9, 0xBC, 0xEF, 0x7A };

START_TEST (gu_table_hash_test)
{
    size_t res;

    ck_assert(sizeof(res) <= 4);

    res = gu_table_hash (test_msg, 0); res = gu_le32(res);
    ck_assert(!check(table_hash32_check0, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 32); res = gu_le32(res);
    ck_assert(!check(table_hash32_check32, &res, sizeof(res)));

    res = gu_table_hash (test_msg, 2011); res = gu_le32(res);
    ck_assert(!check(table_hash32_check2011, &res, sizeof(res)));
}
END_TEST

#else /* GU_WORDSIZE == 32 */
#  error "Unsupported word size"
#endif

#endif // SKIP_TABLE_FUNCTIONS

Suite *gu_digest_suite(void)
{
  Suite *s  = suite_create("gu::Hash");
  TCase *tc = tcase_create("gu_hash");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gu_hash_test);
  tcase_add_test  (tc, gu_fast_hash_test);
//  tcase_add_test  (tc, gu_table_hash_test);

  return s;
}

