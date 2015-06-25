// Copyright (C) 2015 Codership Oy <info@codership.com>

#include "../src/gu_gtid.hpp"

#include <sstream>

#include "gu_gtid_test.hpp"

START_TEST(gtid)
{
    gu::GTID g0;

    fail_if(g0.uuid() != GU_UUID_NIL);
    fail_if(g0.seqno() != gu::GTID::SEQNO_UNDEFINED);
    fail_if(g0.is_undefined() != true);

    gu::UUID const    u(NULL, 0);
    gu::seqno_t const s(1234);

    gu::GTID g1(u, s);

    fail_if(g0 == g1);
    fail_if(g1.uuid()  != u);
    fail_if(g1.seqno() != s);
    fail_if(g1.uuid()  == g0.uuid());
    fail_if(g1.seqno() == g0.seqno());
    fail_if(g1.is_undefined());

    gu::GTID g2(g1);

    fail_if(g1 != g2);

    gu::byte_t buf[27];
    size_t const buflen(sizeof(buf));

    fail_if(buflen < gu::GTID::serial_size());

    size_t const offset(3);
    size_t const offset2(g2.serialize(buf, buflen, offset));
    size_t const offset0(g0.unserialize(buf, buflen, offset));

    fail_if(offset2 != offset0);
    fail_if(offset2 != (offset + gu::GTID::serial_size()));
    fail_if(g0 != g2);

    std::stringstream os;
    os << g0;
    gu::GTID g3;
    os >> g3;

    fail_if(g3 != g1);
    fail_if(g3.uuid()  != u);
    fail_if(g3.seqno() != s);
}
END_TEST

Suite* gu_gtid_suite(void)
{
    Suite* const s(suite_create("gu::GTID"));
    TCase* const t(tcase_create("gtid"));

    suite_add_tcase(s, t);
    tcase_add_test(t, gtid);

    return s;
}
