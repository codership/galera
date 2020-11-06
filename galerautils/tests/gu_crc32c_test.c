/*
 * Copyright (C) 2013-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../src/gu_crc32c.h"

#include "gu_crc32c_test.h"

#include <string.h>

#define long_input                     \
    "0123456789abcdef0123456789ABCDEF" \
    "0123456789abcdef0123456789ABCDEF" \
    "0123456789abcdef0123456789ABCDEF" \
    "0123456789abcdef0123456789ABCDEF" \
    "0123456789abcdef0123456789ABCDEF" \
    "0123456789abcdef0123456789ABCDEF"

#define long_output 0x7e5806b3

struct test_pair
{
    const char* input;
    uint32_t    output;
};

//#define test_vector_length 6

/*
 * boost::crc_optimal<32, 0x1EDC6F41, 0, 0, true, true> crc;
 */
static struct test_pair
test_vector[] =
{
    { "",             0x00000000 },
    { "1",            0x90f599e3 },
    { "22",           0x47b26cf9 },
    { "333",          0x4cb6e5c8 },
    { "4444",         0xfb8150f7 },
    { "55555",        0x23874b2f },
    { "666666",       0xfad65244 },
    { "7777777",      0xe4cbaa36 },
    { "88888888",     0xda8901c2 },
    { "123456789",    0xe3069283 }, // taken from SCTP mailing list
    { "My",           0xc7600404 }, // taken from
    { "test",         0x86a072c0 }, // http://www.zorc.breitbandkatze.de/crc.html
    { "vector",       0xa0b8f38a },
    { long_input,     long_output},
    { NULL,           0x0        }
};

static void
test_function(void)
{
    int i;

    for (i = 0; test_vector[i].input != NULL; i++)
    {
        const char* const input  = test_vector[i].input;
        uint32_t    const output = test_vector[i].output;

        uint32_t ret = gu_crc32c(input, strlen(input));

        ck_assert_msg(ret == output,
                      "Input '%s' resulted in %#08x, expected %#08x\n",
                      input, ret, output);
    }

    const char* const input = long_input;
    uint32_t const   output = long_output;
    int const size = strlen(input);
    int offset = 0;

    gu_crc32c_t crc;

    gu_crc32c_init(&crc);

#define CRC_APPEND(x) gu_crc32c_append(&crc, &input[offset], x); offset += x;

    CRC_APPEND(1);
    CRC_APPEND(3);
    CRC_APPEND(5);
    CRC_APPEND(7);
    CRC_APPEND(13);
    CRC_APPEND(15);
    mark_point();
    CRC_APPEND(0);
    CRC_APPEND(27);
    CRC_APPEND(43);
    CRC_APPEND(64);

    int tail = size - offset;
    ck_assert(tail >= 0);

    CRC_APPEND(tail);

    uint32_t ret = gu_crc32c_get (crc);

    ck_assert_msg(ret == output,
                  "Generated %#08x, expected %#08x\n", ret, output);
}

START_TEST(test_gu_crc32c_sarwate)
{
    gu_crc32c_func = gu_crc32c_sarwate;
    test_function();
}
END_TEST

START_TEST(test_gu_crc32c_slicing_by_4)
{
    gu_crc32c_func = gu_crc32c_slicing_by_4;
    test_function();
}
END_TEST

START_TEST(test_gu_crc32c_slicing_by_8)
{
    gu_crc32c_func = gu_crc32c_slicing_by_8;
    test_function();
}
END_TEST

#if defined(GU_CRC32C_X86)
START_TEST(test_gu_crc32c_x86)
{
    gu_crc32c_func = gu_crc32c_x86;
    test_function();
}
END_TEST

#if defined(GU_CRC32C_X86_64)
START_TEST(test_gu_crc32c_x86_64)
{
    gu_crc32c_func = gu_crc32c_x86_64;
    test_function();
}
END_TEST
#endif /* GU_CRC32C_X86_64 */
#endif /* GU_CRC32C_X86 */

#if defined(GU_CRC32C_ARM64)
START_TEST(test_gu_crc32c_arm64)
{
    gu_crc32c_func = gu_crc32c_hardware();

    if (NULL != gu_crc32c_func)
    {
        ck_assert(gu_crc32c_arm64 == gu_crc32c_func);
        test_function();
    }
}
END_TEST
#endif /* GU_CRC32C_ARM64 */

Suite *gu_crc32c_suite(void)
{
    gu_crc32c_configure(); /* compute lookup tables for SW implementations */

    Suite *suite = suite_create("CRC32C implementations");
    TCase *t;

    t = tcase_create("gu_crc32c_sw");
    suite_add_tcase (suite, t);
    tcase_add_test  (t, test_gu_crc32c_sarwate);
    tcase_add_test  (t, test_gu_crc32c_slicing_by_4);
    tcase_add_test  (t, test_gu_crc32c_slicing_by_8);

#if defined(GU_CRC32C_X86)
    t = tcase_create("gu_crc32c_hw_x86");
    suite_add_tcase (suite, t);
    tcase_add_test  (t, test_gu_crc32c_x86);
#if defined(GU_CRC32C_X86_64)
    tcase_add_test  (t, test_gu_crc32c_x86_64);
#endif /* GU_CRC32C_X86_64 */
#endif /* GU_CRC32C_X86 */

#if defined(GU_CRC32C_ARM64)
    t = tcase_create("gu_crc32c_hw_arm64");
    suite_add_tcase (suite, t);
    tcase_add_test  (t, test_gu_crc32c_arm64);
#endif /* GU_CRC32C_ARM64 */

    return suite;
}
