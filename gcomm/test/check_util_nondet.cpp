/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "gcomm/util.hpp"
#include "gcomm/protonet.hpp"
#include "gcomm/datagram.hpp"
#include "gcomm/conf.hpp"

#include "check_gcomm.hpp"

#include "gu_logger.hpp"

#ifdef HAVE_ASIO_HPP
#include "asio_protonet.hpp"
#endif // HAVE_ASIO_HPP


#include <vector>
#include <fstream>
#include <limits>
#include <cstdlib>
#include <check.h>

using std::vector;
using std::numeric_limits;
using std::string;

using namespace gcomm;
using gu::Exception;
using gu::byte_t;
using gu::Buffer;

#if defined(HAVE_ASIO_HPP)
START_TEST(test_asio)
{
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    AsioProtonet pn(conf);
    string uri_str("tcp://127.0.0.1:0");

    auto acc(pn.acceptor(uri_str));
    acc->listen(uri_str);
    uri_str = acc->listen_addr();

    SocketPtr cl = pn.socket(uri_str);
    cl->connect(uri_str);
    pn.event_loop(gu::datetime::Sec);

    SocketPtr sr = acc->accept();
    ck_assert(sr->state() == Socket::S_CONNECTED);

    vector<byte_t> buf(cl->mtu());
    for (size_t i = 0; i < buf.size(); ++i)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }

    for (size_t i = 0; i < 13; ++i)
    {
        Datagram dg(Buffer(&buf[0], &buf[0] + buf.size()));
        cl->send(0, dg);
    }
    pn.event_loop(gu::datetime::Sec);
}
END_TEST
#endif // HAVE_ASIO_HPP

START_TEST(test_protonet)
{
    gu::Config conf;
    gu::ssl_register_params(conf);
    gcomm::Conf::register_params(conf);
    mark_point();
    Protonet* pn(Protonet::create(conf));
    ck_assert(pn != NULL);
    pn->event_loop(1);
    mark_point();
    delete pn;
}
END_TEST


Suite* util_nondet_suite()
{
    Suite* s = suite_create("util_nondet");
    TCase* tc;

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
