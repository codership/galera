// Copyright (C) 2009 Codership Oy <info@codership.com>

#include <vector>
#include <deque>
#include <algorithm>
#include <functional>
#include <stdexcept>

#include <cstdlib>
#include <cstring>
#include <cassert>

#include "gu_logger.hpp"
#include "gu_uri.hpp"
#include "gu_datagram.hpp"
#include "gu_resolver.hpp"
#include "gu_lock.hpp"
#include "gu_prodcons.hpp"

#include "gu_net_test.hpp"

using std::vector;
using std::string;
using std::deque;
using std::mem_fun;
using std::for_each;
using namespace gu;
using namespace gu::net;
using namespace gu::prodcons;


START_TEST(test_datagram)
{

    // Header check
    NetHeader hdr(42, 0);
    fail_unless(hdr.len() == 42);
    fail_unless(hdr.has_crc32() == false);
    fail_unless(hdr.version() == 0);

    hdr.set_crc32(1234);
    fail_unless(hdr.has_crc32() == true);
    fail_unless(hdr.len() == 42);

    NetHeader hdr1(42, 1);
    fail_unless(hdr1.len() == 42);
    fail_unless(hdr1.has_crc32() == false);
    fail_unless(hdr1.version() == 1);

    byte_t hdrbuf[NetHeader::serial_size_];
    fail_unless(serialize(hdr1, hdrbuf, sizeof(hdrbuf), 0) ==
                NetHeader::serial_size_);
    try
    {
        unserialize(hdrbuf, sizeof(hdrbuf), 0, hdr);
        fail("");
    }
    catch (Exception& e)
    {
        // ok
    }


    byte_t b[128];
    for (byte_t i = 0; i < sizeof(b); ++i)
    {
        b[i] = i;
    }
    Buffer buf(b, b + sizeof(b));

    Datagram dg(buf);
    fail_unless(dg.get_len() == sizeof(b));

    // Normal copy construction
    Datagram dgcopy(buf);
    fail_unless(dgcopy.get_len() == sizeof(b));
    fail_unless(memcmp(dgcopy.get_header() + dgcopy.get_header_offset(),
                       dg.get_header() + dg.get_header_offset(),
                       dg.get_header_len()) == 0);
    fail_unless(dgcopy.get_payload() == dg.get_payload());

    // Copy construction from offset of 16
    Datagram dg16(dg, 16);
    log_info << dg16.get_len();
    fail_unless(dg16.get_len() - dg16.get_offset() == sizeof(b) - 16);
    for (byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        fail_unless(dg16.get_payload()[i + dg16.get_offset()] == i + 16);
    }

#if 0
    // Normalize datagram, all data is moved into payload, data from
    // beginning to offset is discarded. Normalization must not change
    // dg
    dg16.normalize();

    fail_unless(dg16.get_len() == sizeof(b) - 16);
    for (byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        fail_unless(dg16.get_payload()[i] == i + 16);
    }

    fail_unless(dg.get_len() == sizeof(b));
    for (byte_t i = 0; i < sizeof(b); ++i)
    {
        fail_unless(dg.get_payload()[i] == i);
    }

    Datagram dgoff(buf, 16);
    dgoff.get_header().resize(8);
    dgoff.set_header_offset(4);
    fail_unless(dgoff.get_len() == buf.size() + 4);
    fail_unless(dgoff.get_header_offset() == 4);
    fail_unless(dgoff.get_header().size() == 8);
    for (byte_t i = 0; i < 4; ++i)
    {
        *(&dgoff.get_header()[0] + i) = i;
    }

    dgoff.normalize();

    fail_unless(dgoff.get_len() == sizeof(b) - 16 + 4);
    fail_unless(dgoff.get_header_offset() == 0);
    fail_unless(dgoff.get_header().size() == 0);
#endif // 0
}
END_TEST

START_TEST(test_resolver)
{
    std::string tcp_lh4("tcp://127.0.0.1:2002");

    Addrinfo tcp_lh4_ai(resolve(tcp_lh4));
    fail_unless(tcp_lh4_ai.get_family() == AF_INET);
    fail_unless(tcp_lh4_ai.get_socktype() == SOCK_STREAM);

    fail_unless(tcp_lh4_ai.to_string() == tcp_lh4, "%s != %s",
                tcp_lh4_ai.to_string().c_str(), tcp_lh4.c_str());

    std::string tcp_lh6("tcp://[::1]:2002");

    Addrinfo tcp_lh6_ai(resolve(tcp_lh6));
    fail_unless(tcp_lh6_ai.get_family() == AF_INET6);
    fail_unless(tcp_lh6_ai.get_socktype() == SOCK_STREAM);

    fail_unless(tcp_lh6_ai.to_string() == tcp_lh6, "%s != %s",
                tcp_lh6_ai.to_string().c_str(), tcp_lh6.c_str());


    std::string lh("tcp://localhost:2002");
    Addrinfo lh_ai(resolve(lh));
    fail_unless(lh_ai.to_string() == "tcp://127.0.0.1:2002" ||
                lh_ai.to_string() == "tcp://[::1]:2002");

}
END_TEST

START_TEST(trac_288)
{
    try
    {
        string url("tcp://do-not-resolve:0");
        (void)resolve(url);
    }
    catch (Exception& e)
    {
        log_debug << "exception was " << e.what();
    }
}
END_TEST



Suite* gu_net_suite()
{
    Suite* s = suite_create("galerautils++ Networking");
    TCase* tc;

    tc = tcase_create("test_datagram");
    tcase_add_test(tc, test_datagram);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_resolver");
    tcase_add_test(tc, test_resolver);
    suite_add_tcase(s, tc);

    tc = tcase_create("trac_288");
    tcase_add_test(tc, trac_288);
#if 0 /* bogus test, commenting out for now */
    suite_add_tcase(s, tc);
#endif

    return s;
}


