// Copyright (C) 2012 Codership Oy <info@codership.com>

// $Id$

#include "gu_mmh3_test.h"

#include "../src/gu_mmh3.h"
#include "../src/gu_log.h"
#include "../src/gu_print_buf.h"

/* This is to verify all tails plus block + all tails. Max block is 16 bytes */
static const char const test_input[] = "0123456789ABCDEF0123456789abcde";

typedef struct hash32 { uint8_t h[4]; } hash32_t;

#define NUM_32_TESTS 8

static const hash32_t const
test_output32[NUM_32_TESTS] =
{
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }}
};

typedef struct hash128 { uint8_t h[16]; } hash128_t;

#define NUM_128_TESTS 32

static const hash128_t const
test_output128[NUM_128_TESTS] =
{
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }},
    {{ 0, }}
};

typedef void (*hash_f_t) (const void* key, int len, uint32_t seed, void* out);

/* Verification code from the original SMHasher suite */
static void
smhasher_verification (hash_f_t hash, size_t hashbytes, hash32_t* res)
{
    ssize_t const n_tests = 256;
    uint8_t key[n_tests];
    uint8_t hashes[hashbytes * n_tests];
    uint8_t final[hashbytes];

    /* Hash keys of the form {0}, {0,1}, {0,1,2}... up to N=255,using 256-N as
     * the seed */
    ssize_t i;
    for(i = 0; i < n_tests; i++)
    {
        key[i] = (uint8_t)i;
        hash (key, i, n_tests - i, &hashes[i * hashbytes]);
    }

    /* Then hash the result array */
    hash (hashes, hashbytes * n_tests, 0, final);

    memcpy (res, final, sizeof(*res));
}

static hash32_t
smhasher_checks[3] =
{
    {{ 0xE3, 0x7E, 0xF5, 0xB0 }}, /* mmh3_32      */
    {{ 0x2A, 0xE6, 0xEC, 0xB3 }}, /* mmh3_x86_128 */
    {{ 0x69, 0xBA, 0x84, 0x63 }}  /* mmh3_x64_128 */
};

#if 0
/* Verification code for Galera variant of MurmurHash3 - with constant seed */
static void
gu_verification (hash_f_t hash, size_t hashbytes, hash128_t* res)
{
    ssize_t const n_tests = 256;
    uint8_t key[n_tests];
    uint8_t hashes[hashbytes * n_tests];
    uint8_t final[hashbytes];

    /* Hash keys of the form {0}, {0,1}, {0,1,2}... up to N=255, using constant
     * seed */
    ssize_t i;
    for(i = 0; i < n_tests; i++)
    {
        key[i] = (uint8_t)i;
        hash (key, i, n_tests - i, &hashes[i * hashbytes]);
    }

    /* Then hash the result array */
    hash (hashes, hashbytes * n_tests, 0, final);

    memcpy (res, final, hashbytes);
}

static hash128_t
gu_checks[3] =
{
    {{ 0xE3, 0x7E, 0xF5, 0xB0, }}, /* mmh3_32      */
    {{ 0x2A, 0xE6, 0xEC, 0xB3, }}, /* mmh3_x86_128 */
    {{ 0x69, 0xBA, 0x84, 0x63, }}  /* mmh3_x64_128 */
};
#endif

/* returns true if check fails */
static bool
check (const void* const exp, const void* const got, ssize_t size)
{
    if (memcmp (exp, got, size))
    {
        ssize_t str_size = size * 1.2 + 1;
        char c[str_size], r[str_size];

        gu_print_buf (exp, size, c, sizeof(c), false);
        gu_print_buf (got, size, r, sizeof(r), false);

        gu_info ("expected MurmurHash3:\n%s\nfound:\n%s\n", c, r);

        return true;
    }

    return false;
}

START_TEST (gu_mmh32_test)
{
    int i;
    hash32_t out;

    smhasher_verification (gu_mmh3_32, sizeof(hash32_t), &out);
    fail_if (check (&smhasher_checks[0], &out, sizeof(out)),
             "gu_mmh3_32 failed.");

    for (i = 0; i < NUM_32_TESTS; i++)
    {
        gu_mmh3_32 (test_input, i, 0, &out);
        check (&test_output32[i], &out, sizeof(out));
    }
}
END_TEST

START_TEST (gu_mmh128_x86_test)
{
    int i;
    hash32_t out32;

    smhasher_verification (gu_mmh3_x86_128, sizeof(hash128_t), &out32);
    fail_if (check (&smhasher_checks[1], &out32, sizeof(out32)),
             "gu_mmh3_x86_128 failed.");

    for (i = 0; i < NUM_128_TESTS; i++)
    {
        hash128_t out;
        gu_mmh3_x86_128 (test_input, i, 0, &out);
        check (&test_output128[i], &out, sizeof(out));
    }
}
END_TEST

START_TEST (gu_mmh128_x64_test)
{
    int i;
    hash32_t out32;

    smhasher_verification (gu_mmh3_x64_128, sizeof(hash128_t), &out32);
    fail_if (check (&smhasher_checks[2], &out32, sizeof(out32)),
             "gu_mmh3_x64_128 failed.");

    for (i = 0; i < NUM_128_TESTS; i++)
    {
        hash128_t out;
        gu_mmh3_x64_128 (test_input, i, 0, &out);
        check (&test_output128[i], &out, sizeof(out));
    }
}
END_TEST

Suite *gu_mmh3_suite(void)
{
  Suite *s  = suite_create("Galera MurmurHash3");
  TCase *tc = tcase_create("gu_mmh3");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, gu_mmh32_test);
  tcase_add_test (tc, gu_mmh128_x86_test);
  tcase_add_test (tc, gu_mmh128_x64_test);

  return s;
}

