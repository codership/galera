/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "gcomm/util.hpp"
#include "gcomm/protonet.hpp"
#include "gcomm/datagram.hpp"
#include "gcomm/conf.hpp"

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
    ck_assert(hdr.len() == 42);
    ck_assert(hdr.has_crc32() == false);
    ck_assert(hdr.version() == 0);

    hdr.set_crc32(1234, NetHeader::CS_CRC32);
    ck_assert(hdr.has_crc32() == true);
    ck_assert(hdr.len() == 42);

    gcomm::NetHeader hdr1(42, 1);
    ck_assert(hdr1.len() == 42);
    ck_assert(hdr1.has_crc32() == false);
    ck_assert(hdr1.version() == 1);

    gu::byte_t hdrbuf[NetHeader::serial_size_];
    ck_assert(serialize(hdr1, hdrbuf, sizeof(hdrbuf), 0) ==
                NetHeader::serial_size_);
    try
    {
        unserialize(hdrbuf, sizeof(hdrbuf), 0, hdr);
        ck_abort();
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
    ck_assert(dg.len() == sizeof(b));

    // Normal copy construction
    gcomm::Datagram dgcopy(buf);
    ck_assert(dgcopy.len() == sizeof(b));
    ck_assert(memcmp(dgcopy.header() + dgcopy.header_offset(),
                       dg.header() + dg.header_offset(),
                       dg.header_len()) == 0);
    ck_assert(dgcopy.payload() == dg.payload());

    // Copy construction from offset of 16
    gcomm::Datagram dg16(dg, 16);
    log_info << dg16.len();
    ck_assert(dg16.len() - dg16.offset() == sizeof(b) - 16);
    for (gu::byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        ck_assert(dg16.payload()[i + dg16.offset()] == i + 16);
    }

#if 0
    // Normalize datagram, all data is moved into payload, data from
    // beginning to offset is discarded. Normalization must not change
    // dg
    dg16.normalize();

    ck_assert(dg16.len() == sizeof(b) - 16);
    for (byte_t i = 0; i < sizeof(b) - 16; ++i)
    {
        ck_assert(dg16.payload()[i] == i + 16);
    }

    ck_assert(dg.len() == sizeof(b));
    for (byte_t i = 0; i < sizeof(b); ++i)
    {
        ck_assert(dg.payload()[i] == i);
    }

    Datagram dgoff(buf, 16);
    dgoff.header().resize(8);
    dgoff.set_header_offset(4);
    ck_assert(dgoff.len() == buf.size() + 4);
    ck_assert(dgoff.header_offset() == 4);
    ck_assert(dgoff.header().size() == 8);
    for (byte_t i = 0; i < 4; ++i)
    {
        *(&dgoff.header()[0] + i) = i;
    }

    dgoff.normalize();

    ck_assert(dgoff.len() == sizeof(b) - 16 + 4);
    ck_assert(dgoff.header_offset() == 0);
    ck_assert(dgoff.header().size() == 0);
#endif // 0
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

        ck_assert(view == view2);
    }

    // Create configuration to set file name.
    gu::Config conf;

    // compare view state.
    UUID my_uuid(NULL, 0);
    ViewState vst(my_uuid, view, conf);
    UUID my_uuid_2;
    View view_2;
    ViewState vst2(my_uuid_2, view_2, conf);

    {
        std::ostringstream os;
        vst.write_stream(os);

        std::istringstream is(os.str());
        vst2.read_stream(is);

        ck_assert(vst == vst2);
    }

    // test write file and read file.
    vst.write_file();
    UUID my_uuid_3;
    View view_3;
    ViewState vst3(my_uuid_3, view_3, conf);
    vst3.read_file();
    ck_assert(vst == vst3);
    ViewState::remove_file(conf);
}
END_TEST


Suite* util_suite()
{
    Suite* s = suite_create("util");
    TCase* tc;

    tc = tcase_create("test_datagram");
    tcase_add_test(tc, test_datagram);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_view_state");
    tcase_add_test(tc, test_view_state);
    suite_add_tcase(s, tc);

    return s;
}
