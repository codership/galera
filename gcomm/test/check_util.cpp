/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#include "gcomm/util.hpp"
#include "gcomm/protonet.hpp"
#include "gcomm/datagram.hpp"
#include "gcomm/conf.hpp"

#ifdef HAVE_ASIO_HPP
#include "asio_protonet.hpp"
#endif // HAVE_ASIO_HPP

#include "check_gcomm.hpp"

#include "gu_logger.hpp"

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

START_TEST(test_datagram)
{

    // Header check
    gcomm::NetHeader hdr(42, 0);
    fail_unless(hdr.len() == 42);
    fail_unless(hdr.has_crc32() == false);
    fail_unless(hdr.version() == 0);

    hdr.set_crc32(1234, NetHeader::CS_CRC32);
    fail_unless(hdr.has_crc32() == true);
    fail_unless(hdr.len() == 42);

    gcomm::NetHeader hdr1(42, 1);
    fail_unless(hdr1.len() == 42);
    fail_unless(hdr1.has_crc32() == false);
    fail_unless(hdr1.version() == 1);

    gu::byte_t hdrbuf[NetHeader::serial_size_];
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


    gu::byte_t b[128];
    for (gu::byte_t i = 0; i < sizeof(b); ++i)
    {
        b[i] = i;
    }
    gu::Buffer buf(b, b + sizeof(b));

    gcomm::Datagram dg(buf);
    fail_unless(dg.len() == sizeof(b));

    // Normal copy construction
    gcomm::Datagram dgcopy(buf);
    fail_unless(dgcopy.len() == sizeof(b));
    fail_unless(memcmp(dgcopy.header() + dgcopy.header_offset(),
                       dg.header() + dg.header_offset(),
                       dg.header_len()) == 0);
    fail_unless(dgcopy.payload() == dg.payload());

    // Copy construction from offset of 16
    gcomm::Datagram dg16(dg, 16);
    log_info << dg16.len();
    fail_unless(dg16.len() - dg16.offset() == sizeof(b) - 16);
    for (gu::byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        fail_unless(dg16.payload()[i + dg16.offset()] == i + 16);
    }

#if 0
    // Normalize datagram, all data is moved into payload, data from
    // beginning to offset is discarded. Normalization must not change
    // dg
    dg16.normalize();

    fail_unless(dg16.len() == sizeof(b) - 16);
    for (byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        fail_unless(dg16.payload()[i] == i + 16);
    }

    fail_unless(dg.len() == sizeof(b));
    for (byte_t i = 0; i < sizeof(b); ++i)
    {
        fail_unless(dg.payload()[i] == i);
    }

    Datagram dgoff(buf, 16);
    dgoff.header().resize(8);
    dgoff.set_header_offset(4);
    fail_unless(dgoff.len() == buf.size() + 4);
    fail_unless(dgoff.header_offset() == 4);
    fail_unless(dgoff.header().size() == 8);
    for (byte_t i = 0; i < 4; ++i)
    {
        *(&dgoff.header()[0] + i) = i;
    }

    dgoff.normalize();

    fail_unless(dgoff.len() == sizeof(b) - 16 + 4);
    fail_unless(dgoff.header_offset() == 0);
    fail_unless(dgoff.header().size() == 0);
#endif // 0
}
END_TEST


#if defined(HAVE_ASIO_HPP)
START_TEST(test_asio)
{
    gu::Config conf;
    gcomm::Conf::register_params(conf);
    AsioProtonet pn(conf);
    string uri_str("tcp://127.0.0.1:0");

    Acceptor* acc = pn.acceptor(uri_str);
    acc->listen(uri_str);
    uri_str = acc->listen_addr();

    SocketPtr cl = pn.socket(uri_str);
    cl->connect(uri_str);
    pn.event_loop(gu::datetime::Sec);

    SocketPtr sr = acc->accept();
    fail_unless(sr->state() == Socket::S_CONNECTED);

    vector<byte_t> buf(cl->mtu());
    for (size_t i = 0; i < buf.size(); ++i)
    {
        buf[i] = static_cast<byte_t>(i & 0xff);
    }

    for (size_t i = 0; i < 13; ++i)
    {
        Datagram dg(Buffer(&buf[0], &buf[0] + buf.size()));
        cl->send(dg);
    }
    pn.event_loop(gu::datetime::Sec);

    delete acc;

}
END_TEST
#endif // HAVE_ASIO_HPP

START_TEST(test_protonet)
{
    gu::Config conf;
    gcomm::Conf::register_params(conf);
    Protonet* pn(Protonet::create(conf));
    pn->event_loop(1);
}
END_TEST

START_TEST(test_view_state)
{
    // compare view.
    UUID view_uuid(NULL, 0);
    ViewId view_id(V_TRANS, view_uuid, 789);
    UUID m1(NULL, 0);
    UUID m2(NULL, 0);
    View view(0, view_id, true);
    view.add_member(m1, 0);
    view.add_member(m2, 1);
    View view2;

    {
        std::ostringstream os;
        view.write_stream(os);

        std::istringstream is(os.str());
        view2.read_stream(is);

        fail_unless(view == view2);
    }

    // compare view state.
    UUID my_uuid(NULL, 0);
    ViewState vst(my_uuid, view);
    UUID my_uuid_2;
    View view_2;
    ViewState vst2(my_uuid_2, view_2);

    {
        std::ostringstream os;
        vst.write_stream(os);

        std::istringstream is(os.str());
        vst2.read_stream(is);

        fail_unless(vst == vst2);
    }

    const char* fname = "/tmp/gvwstate.dat";
    // test write file and read file.
    vst.write_file(fname);
    UUID my_uuid_3;
    View view_3;
    ViewState vst3(my_uuid_3, view_3);
    vst3.read_file(fname);
    fail_unless(vst == vst3);
    unlink(fname);
}
END_TEST


Suite* util_suite()
{
    Suite* s = suite_create("util");
    TCase* tc;

    tc = tcase_create("test_datagram");
    tcase_add_test(tc, test_datagram);
    suite_add_tcase(s, tc);

#ifdef HAVE_ASIO_HPP
    tc = tcase_create("test_asio");
    tcase_add_test(tc, test_asio);
    suite_add_tcase(s, tc);
#endif // HAVE_ASIO_HPP

    tc = tcase_create("test_protonet");
    tcase_add_test(tc, test_protonet);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_view_state");
    tcase_add_test(tc, test_view_state);
    suite_add_tcase(s, tc);

    return s;
}
