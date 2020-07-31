/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */


#include "check_gcomm.hpp"

#include "gcomm/view.hpp"
#include "gcomm/types.hpp"
#include "gcomm/map.hpp"

#include <utility>
#include <iostream>
#include <stdexcept>

using std::pair;
using std::make_pair;
using std::string;

#include "check_templ.hpp"

#include <check.h>

using namespace gcomm;

START_TEST(test_uuid)
{
    UUID uuid;
    ck_assert_msg(uuid.full_str() == "00000000-0000-0000-0000-000000000000",
                  "%s", uuid.full_str().c_str());
    for (size_t i = 0; i < 159; ++i)
    {
        UUID uuidrnd(0, 0);
        log_debug << uuidrnd;
    }

    UUID uuid1(0, 0);
    UUID uuid2(0, 0);

    ck_assert(uuid1 < uuid2);

    // Verify that short UUID notation matches with first 8 chars
    // of full uuid string.
    std::string full(uuid1.full_str());
    std::ostringstream os;
    os << uuid1;
    ck_assert_msg(full.compare(0, 8, os.str()) == 0,
                  "%s != %s", full.c_str(), os.str().c_str());

}
END_TEST


START_TEST(test_view)
{
    const UUID uuid1(1);
    const UUID uuid2(2);
    const UUID uuid3(3);

    // View id ordering:
    // 1) view seq less than
    // 2) uuid newer than (higher timestamp, greater leading bytes)
    // 3) view type (reg, trans, non-prim, prim)
    ViewId v1(V_REG,   uuid2, 1);
    ViewId v2(V_REG,   uuid1, 1);
    ViewId v3(V_TRANS, uuid1, 1);

    ViewId v4(V_TRANS, uuid3, 2);
    ViewId v5(V_REG,   uuid2, 2);

    ViewId v6(V_REG,   uuid1, 2);
    ViewId v7(V_TRANS, uuid1, 2);

    ck_assert(v1 < v2);
    ck_assert(v2 < v3);
    ck_assert(v3 < v4);
    ck_assert(v4 < v5);
    ck_assert(v5 < v6);
    ck_assert(v6 < v7);



    ViewId vid;
    ck_assert(vid.uuid() == UUID());
    ck_assert(vid.seq() == 0);

    UUID uuid(0, 0);

    vid = ViewId(V_REG, uuid, 7);
    ck_assert(vid.uuid() == uuid);
    ck_assert(vid.seq() == 7);


    NodeList nl;

    for (size_t i = 0; i < 7; ++i)
    {
        nl.insert(make_pair(UUID(0, 0), Node(0)));
    }

    ck_assert(nl.size() == 7);

}
END_TEST


Suite* types_suite()
{
    Suite* s = suite_create("types");
    TCase* tc;

    tc = tcase_create("test_uuid");
    tcase_add_test(tc, test_uuid);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_view");
    tcase_add_test(tc, test_view);
    suite_add_tcase(s, tc);

    return s;
}
