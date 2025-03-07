/*
 * Copyright (C) 2025 Codership Oy <info@galeracluster.com>
 */

#include "check_gcomm.hpp"

#include "gmcast.hpp"

#include <check.h>

START_TEST(test_gmcast_empty_relay_set)
{
    log_info << "START test_gmcast_empty_relay_set";

    std::set<gcomm::gmcast::Proto*> proto_set;
    std::set<gcomm::UUID> nonlive_uuids;
    /* todo: populate proto_map with some protos */
    gcomm::GMCast::RelaySet relay_set = gcomm::GMCast::compute_relay_set(proto_set, nonlive_uuids, 0);

    ck_assert(relay_set.empty());
}
END_TEST


Suite* gmcast_suite()
{
    Suite* s = suite_create("gmcast");
    TCase* tc;

    tc = tcase_create("test_gmcast_empty_relay_set");
    tcase_add_test(tc, test_gmcast_empty_relay_set);
    suite_add_tcase(s, tc);

    return s;
}
