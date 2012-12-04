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


