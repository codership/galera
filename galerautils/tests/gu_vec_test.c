/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gu_vec_test.h"

#include "../src/gu_vec16.h"

START_TEST (vec16_test)
{
    gu_vec16_t v1 = gu_vec16_from_byte (0);
    gu_vec16_t v2 = gu_vec16_from_byte (0);
    gu_vec16_t v3 = gu_vec16_from_byte (7);

    fail_if (!gu_vec16_eq(v1, v2));
    fail_if (gu_vec16_eq(v1, v3));

    unsigned char a1[16], a2[16], a3[16];

    fail_if (sizeof(v1) != sizeof(a1));

    unsigned int i;
    for (i = 0; i < sizeof(a1); i++)
    {
        a1[i] = i;
        a2[i] = i * i;
        a3[i] = a1[i] ^ a2[i];
    }

    v1 = gu_vec16_from_ptr (a1);
    v2 = gu_vec16_from_ptr (a2);

    fail_if (gu_vec16_eq(v1, v2));

    v3 = gu_vec16_xor (v1, v2);

    fail_if (memcmp (&v3, a3, sizeof(a3)));
}
END_TEST

Suite*
gu_vec_suite(void)
{
    TCase* t = tcase_create ("vec16");
    tcase_add_test (t, vec16_test);

    Suite* s = suite_create ("Vector math");
    suite_add_tcase (s, t);

    return s;
}

