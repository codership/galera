/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/util.hpp"
#include "histogram.hpp"
#include "gcomm/protonet.hpp"

#ifdef HAVE_ASIO_HPP
#include "asio_protonet.hpp"
#endif // HAVE_ASIO_HPP

#include "check_gcomm.hpp"

#include "gu_string.hpp"
#include "gu_logger.hpp"


#include <vector>
#include <limits>
#include <cstdlib>
#include <check.h>

using std::vector;
using std::numeric_limits;
using std::string;

using namespace gcomm;

using namespace gu;



START_TEST(test_histogram)
{

    Histogram hs("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.");

    hs.insert(0.001);
    log_info << hs;

    for (size_t i = 0; i < 1000; ++i)
    {
        hs.insert(double(::rand())/RAND_MAX);
    }

    log_info << hs;

    hs.clear();

    log_info << hs;
}
END_TEST


#if defined(HAVE_ASIO_HPP)
START_TEST(test_asio)
{
    gu::Config conf;
    AsioProtonet pn(conf);
    string uri_str("tcp://localhost:0");

    Acceptor* acc = pn.acceptor(uri_str);
    acc->listen(uri_str);
    uri_str = acc->listen_addr();

    SocketPtr cl = pn.socket(uri_str);
    cl->connect(uri_str);
    pn.event_loop(datetime::Sec);

    SocketPtr sr = acc->accept();
    fail_unless(sr->get_state() == Socket::S_CONNECTED);

    vector<byte_t> buf(cl->get_mtu());
    for (size_t i = 0; i < buf.size(); ++i)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }

    for (size_t i = 0; i < 13; ++i)
    {
        Datagram dg(Buffer(&buf[0], &buf[0] + buf.size()));
        cl->send(dg);
    }
    pn.event_loop(datetime::Sec);

    delete acc;

}
END_TEST
#endif // HAVE_ASIO_HPP

START_TEST(test_protonet)
{
    gu::Config conf;
    Protonet* pn(Protonet::create(conf));
    pn->event_loop(1);
}
END_TEST


Suite* util_suite()
{
    Suite* s = suite_create("util");
    TCase* tc;

    tc = tcase_create("test_histogram");
    tcase_add_test(tc, test_histogram);
    suite_add_tcase(s, tc);

#ifdef HAVE_ASIO_HPP
    tc = tcase_create("test_asio");
    tcase_add_test(tc, test_asio);
    suite_add_tcase(s, tc);
#endif // HAVE_ASIO_HPP

    tc = tcase_create("test_protonet");
    tcase_add_test(tc, test_protonet);
    suite_add_tcase(s, tc);

    return s;
}
