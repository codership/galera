// Copyright (C) 2012-2020 Codership Oy <info@codership.com>

// $Id$

#include "gu_fnv_test.h"

#include <inttypes.h>
#include <string.h>

static const char* const test_buf = "chongo <Landon Curt Noll> /\\../\\";

// enable normal FNV mode for reference hash checking
#define GU_FNV_NORMAL

#include "../src/gu_fnv.h"

START_TEST (gu_fnv32_test)
{
    uint32_t ret = 0;
    gu_fnv32a_internal (test_buf, strlen(test_buf), &ret);
    ck_assert_msg(GU_FNV32_SEED == ret,
                  "FNV32 failed: expected %lu, got %"PRIu32,
                  GU_FNV32_SEED, ret);
}
END_TEST

START_TEST (gu_fnv64_test)
{
    uint64_t ret = 0;
    gu_fnv64a_internal (test_buf, strlen(test_buf), &ret);
    ck_assert_msg(GU_FNV64_SEED == ret,
                  "FNV64 failed: expected %llu, got %"PRIu64,
                  GU_FNV64_SEED, ret);
}
END_TEST

START_TEST (gu_fnv128_test)
{
    gu_uint128_t GU_SET128(ret, 0, 0);
    gu_fnv128a_internal (test_buf, strlen(test_buf), &ret);
#if defined(__SIZEOF_INT128__)
    ck_assert_msg(GU_EQ128(GU_FNV128_SEED, ret),
                  "FNV128 failed: expected %"PRIx64" %"PRIx64", got %"PRIx64" %"PRIx64,
                  (uint64_t)(GU_FNV128_SEED >> 64), (uint64_t)GU_FNV128_SEED,
                  (uint64_t)(ret >> 64), (uint64_t)ret);
#else
    ck_assert_msg(GU_EQ128(GU_FNV128_SEED, ret),
                  "FNV128 failed: expected %"PRIx64" %"PRIx64", got %"PRIx64" %"PRIx64,
                  GU_FNV128_SEED.u64[GU_64HI], GU_FNV128_SEED.u64[GU_64LO],
                  ret.u64[GU_64HI], ret.u64[GU_64LO]);
#endif
}
END_TEST

Suite *gu_fnv_suite(void)
{
  Suite *s = suite_create("FNV hash");
  TCase *tc_fnv = tcase_create("gu_fnv");

  suite_add_tcase (s, tc_fnv);
  tcase_add_test(tc_fnv, gu_fnv32_test);
  tcase_add_test(tc_fnv, gu_fnv64_test);
  tcase_add_test(tc_fnv, gu_fnv128_test);
  return s;
}

