/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#undef NDEBUG

#include "test_key.hpp"
#include "../src/write_set_ng.hpp"

#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include <check.h>

using namespace galera;

START_TEST (ver0_basic)
{
    WriteSetNG::Version const ws_ver(WriteSetNG::VER0);
    KeySet::Version const     tk_ver(WriteSetNG::ws_to_ks_version(ws_ver));

    WriteSetOut wso ("", ws_ver);

    fail_unless (wso.is_empty());

    TestKey tk0(tk_ver, SHARED, true, "a0");
    wso.append_key(tk0());
    fail_if (wso.is_empty());

    uint64_t const data_out_volatile(0xaabbccdd);
    uint32_t const data_out_persistent(0xffeeddcc);

    {
        uint64_t const d(data_out_volatile);
        wso.append_data (&d, sizeof(d), true);
    }

    wso.append_data (&data_out_persistent, sizeof(data_out_persistent), false);

    std::vector<gu::Buf> out;
    size_t const out_size(wso.gather(out));

    log_info << "Gather size: " << out_size << ", buf count: " << out.size();

    wsrep_seqno_t const last_seen(1);
    wso.set_last_seen(last_seen);

    /* concatenate all out buffers */
    std::vector<gu::byte_t> in;
    in.reserve(out_size);
    for (size_t i(0); i < out.size(); ++i)
    {
        in.insert (in.end(), out[i].ptr, out[i].ptr + out[i].size);
    }

    fail_if (in.size() != out_size);

    gu::Buf in_buf = { in.data(), static_cast<ssize_t>(in.size()) };

    try
    {
        mark_point();
        WriteSetIn wsi(in_buf);

        mark_point();
        wsrep_seqno_t const ls(wsi.last_seen());
        fail_if (ls != last_seen, "Found last seen: %lld, expected: %lld",
                 ls, last_seen);
        fail_if (0 == wsi.timestamp());

        mark_point();
        const KeySetIn* const ksi(wsi.keyset());
        fail_if (NULL == ksi);
        fail_if (ksi->count() != 1);

        mark_point();
        for (int i(0); i < ksi->count(); ++i)
        {
            KeySet::KeyPart kp(ksi->next());
        }

        mark_point();
        wsi.verify_checksum();

        mark_point();
        const DataSetIn* const dsi(wsi.dataset());
        fail_if (NULL == dsi);
        fail_if (dsi->count() != 2);

        mark_point();
        gu::Buf const d1(dsi->next());
        fail_if (d1.size != sizeof(data_out_volatile));
        fail_if (*(reinterpret_cast<const uint64_t*>(d1.ptr)) !=
                 data_out_volatile);

        mark_point();
        gu::Buf const d2(dsi->next());
        fail_if (d2.size != sizeof(data_out_persistent));
        fail_if (*(reinterpret_cast<const uint32_t*>(d2.ptr)) !=
                 data_out_persistent);

        mark_point();
        const DataSetIn* const usi(wsi.unrdset());
        fail_if (NULL != usi);
    }
    catch (std::exception& e)
    {
        fail("%s", e.what());
    }

    in[in.size() - 1] ^= 1; // corrupted the last byte (payload)

    try /* this is to test payload corruption */
    {
        WriteSetIn wsi(in_buf);
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
    }

    try /* this is to test postponed checksumming + corruption */
    {
        WriteSetIn wsi(in_buf, 2);

        mark_point();

        try {
            wsi.verify_checksum();
            fail("payload corruption slipped through");
        }
        catch (gu::Exception& e)
        {
            fail_if (e.get_errno() != EINVAL);
        }
    }
    catch (std::exception& e)
    {
        fail("%s", e.what());
    }

    in[2] ^= 1; // corrupted 3rd byte of header

    try /* this is to test header corruption */
    {
        WriteSetIn wsi(in_buf, 2 /* this should postpone payload checksum */);
        fail("header corruption slipped through");
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
    }
}
END_TEST

Suite* write_set_ng_suite ()
{
    TCase* t = tcase_create ("WriteSet");
    tcase_add_test (t, ver0_basic);
    tcase_set_timeout(t, 60);

    Suite* s = suite_create ("WriteSet");
    suite_add_tcase (s, t);

    return s;
}
