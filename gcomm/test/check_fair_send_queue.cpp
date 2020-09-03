//
// Copyright (C) 2019-2020 Codership Oy <info@codership.com>
//

#include "check_gcomm.hpp"
#include "fair_send_queue.hpp"

#include <check.h>

static gcomm::Datagram make_datagram(char header)
{
    static const char data[1] = { 0 };
    gcomm::Datagram ret(gu::SharedBuffer(new gu::Buffer(data, data + 1)));
    ret.set_header_offset(ret.header_offset() - 1);
    ret.header()[ret.header_offset()] = header;
    return ret;
}

static gu::byte_t get_header(const gcomm::Datagram& dg)
{
    return dg.header()[dg.header_offset()];
}

// Test the make_datagram() helper above.
START_TEST(test_datagram)
{
    gcomm::Datagram dg(make_datagram(1));
    ck_assert(dg.len() == 2);
    ck_assert(get_header(dg) == 1);
}
END_TEST

START_TEST(test_push_back)
{
    gcomm::FairSendQueue fsq;
    gcomm::Datagram dg(make_datagram(1));
    fsq.push_back(0, dg);
    ck_assert(fsq.front().len() == 2);
    ck_assert(get_header(fsq.front()) == 1);
}
END_TEST

START_TEST(test_push_back_same_segments)
{
    gcomm::FairSendQueue fsq;
    gcomm::Datagram dg(make_datagram(1));
    fsq.push_back(0, dg);
    ck_assert(get_header(fsq.front()) == 1);
    ck_assert(get_header(fsq.back()) == 1);
    fsq.push_back(0, make_datagram(2));
    ck_assert(get_header(fsq.front()) == 1);
    ck_assert(get_header(fsq.back()) == 2);
}
END_TEST

START_TEST(test_push_back_different_segments)
{
    gcomm::FairSendQueue fsq;
    gcomm::Datagram dg(make_datagram(1));
    fsq.push_back(0, dg);
    ck_assert(get_header(fsq.front()) == 1);
    ck_assert(get_header(fsq.back()) == 1);
    fsq.push_back(1, make_datagram(2));
    ck_assert(get_header(fsq.front()) == 1);
    ck_assert(get_header(fsq.back()) == 2);
}
END_TEST

START_TEST(test_empty)
{
    gcomm::FairSendQueue fsq;
    ck_assert(fsq.empty());
    fsq.push_back(0, make_datagram(1));
    ck_assert(!fsq.empty());
}
END_TEST

START_TEST(test_size)
{
    gcomm::FairSendQueue fsq;
    fsq.push_back(0, make_datagram(1));
    ck_assert(fsq.size() == 1);
    fsq.push_back(1, make_datagram(2));
    ck_assert(fsq.size() == 2);
}
END_TEST

START_TEST(test_pop_front)
{
    gcomm::FairSendQueue fsq;
    fsq.push_back(0, make_datagram(1));
    ck_assert(fsq.size() == 1);
    fsq.pop_front();
    ck_assert(fsq.size() == 0);
}
END_TEST

START_TEST(test_pop_front_same_segments)
{
    gcomm::FairSendQueue fsq;
    fsq.push_back(0, make_datagram(1));
    ck_assert(fsq.size() == 1);
    fsq.push_back(0, make_datagram(2));
    ck_assert(fsq.size() == 2);
    ck_assert(get_header(fsq.front()) == 1);

    fsq.pop_front();
    ck_assert(fsq.size() == 1);
    ck_assert(get_header(fsq.front()) == 2);
}
END_TEST

START_TEST(test_pop_front_different_segments)
{
    gcomm::FairSendQueue fsq;
    fsq.push_back(0, make_datagram(1));
    ck_assert(fsq.size() == 1);
    fsq.push_back(1, make_datagram(2));
    ck_assert(fsq.size() == 2);
    ck_assert(get_header(fsq.front()) == 1);

    fsq.pop_front();
    ck_assert(fsq.size() == 1);
    ck_assert(get_header(fsq.front()) == 2);
}
END_TEST

START_TEST(test_push_pop_interleaving)
{
    gcomm::FairSendQueue fsq;
    fsq.push_back(0, make_datagram(1));
    fsq.push_back(1, make_datagram(2));
    fsq.push_back(0, make_datagram(3));
    fsq.push_back(1, make_datagram(4));

    ck_assert(get_header(fsq.front()) == 1);
    ck_assert(fsq.size() == 4);

    fsq.pop_front();
    ck_assert(fsq.size() == 3);
    ck_assert(get_header(fsq.front()) == 2);
    fsq.pop_front();
    ck_assert(fsq.size() == 2);
    ck_assert(get_header(fsq.front()) == 3);
    fsq.pop_front();
    ck_assert(fsq.size() == 1);
    ck_assert(get_header(fsq.front()) == 4);
    fsq.pop_front();
    ck_assert(fsq.empty());
}
END_TEST

START_TEST(test_queued_bytes)
{
    gcomm::FairSendQueue fsq;
    fsq.push_back(0, make_datagram(1));
    ck_assert(fsq.queued_bytes() == 2);
    fsq.push_back(1, make_datagram(2));
    ck_assert(fsq.queued_bytes() == 4);
    fsq.pop_front();
    ck_assert(fsq.queued_bytes() == 2);
    fsq.pop_front();
    ck_assert(fsq.queued_bytes() == 0);
}
END_TEST


Suite* fair_send_queue_suite()
{
    Suite* ret(suite_create("fair_send_queue"));
    TCase* tc;

    tc = tcase_create("test_datagram");
    tcase_add_test(tc, test_datagram);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_push_back");
    tcase_add_test(tc, test_push_back);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_push_back_same_segments");
    tcase_add_test(tc, test_push_back_same_segments);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_push_back_different_segments");
    tcase_add_test(tc, test_push_back_different_segments);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_empty");
    tcase_add_test(tc, test_empty);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_size");
    tcase_add_test(tc, test_size);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_pop_front");
    tcase_add_test(tc, test_pop_front);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_pop_front_same_segments");
    tcase_add_test(tc, test_pop_front_same_segments);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_pop_front_different_segments");
    tcase_add_test(tc, test_pop_front_different_segments);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_push_pop_interleaving");
    tcase_add_test(tc, test_push_pop_interleaving);
    suite_add_tcase(ret, tc);

    tc = tcase_create("test_queued_bytes");
    tcase_add_test(tc, test_queued_bytes);
    suite_add_tcase(ret, tc);

    return ret;
}
