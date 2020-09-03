// Copyright (C) 2007-2020 Codership Oy <info@codership.com>

// $Id$

#include <stdint.h>
#include <check.h>
#include "gu_bswap_test.h"
#include "../src/gu_byteswap.h"

START_TEST (gu_bswap_test)
{
    // need volatile to prevent compile-time optimization
    volatile uint16_t s = 0x1234;
    volatile uint32_t i = 0x12345678;
    volatile uint64_t l = 0x1827364554637281LL;

    uint16_t sle, sbe;
    uint32_t ile, ibe;
    uint64_t lle, lbe;

    // first conversion
    sle = gu_le16(s); sbe = gu_be16(s);
    ile = gu_le32(i); ibe = gu_be32(i);
    lle = gu_le64(l); lbe = gu_be64(l);

#if __BYTE_ORDER == __LITTLE_ENDIAN
    ck_assert(s == sle);
    ck_assert(i == ile);
    ck_assert(l == lle);
    ck_assert(s != sbe);
    ck_assert(i != ibe);
    ck_assert(l != lbe);
#else
    ck_assert(s != sle);
    ck_assert(i != ile);
    ck_assert(l != lle);
    ck_assert(s == sbe);
    ck_assert(i == ibe);
    ck_assert(l == lbe);
#endif /* __BYTE_ORDER */

    // second conversion
    sle = gu_le16(sle); sbe = gu_be16(sbe);
    ile = gu_le32(ile); ibe = gu_be32(ibe);
    lle = gu_le64(lle); lbe = gu_be64(lbe);

    ck_assert(s == sle);
    ck_assert(i == ile);
    ck_assert(l == lle);
    ck_assert(s == sbe);
    ck_assert(i == ibe);
    ck_assert(l == lbe);

}
END_TEST

Suite *gu_bswap_suite(void)
{
  Suite *s  = suite_create("Galera byteswap functions");
  TCase *tc = tcase_create("gu_bswap");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gu_bswap_test);
  return s;
}

