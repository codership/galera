/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#undef NDEBUG

#include "test_key.hpp"
#include "../src/write_set_ng.hpp"

#include "gu_uuid.h"
#include "gu_logger.hpp"
#include "gu_hexdump.hpp"

#include <check.h>

using namespace galera;

START_TEST (ver3_basic)
{
    uint16_t const flag1(0xabcd);
    wsrep_uuid_t source;
    gu_uuid_generate (reinterpret_cast<gu_uuid_t*>(&source), NULL, 0);
    wsrep_conn_id_t const conn(652653);
    wsrep_trx_id_t const  trx(99994952);

    std::string const dir(".");
    wsrep_trx_id_t trx_id(1);
    WriteSetOut wso (dir, trx_id, KeySet::FLAT8A, 0, 0, flag1,WriteSetNG::VER3);

    fail_unless (wso.is_empty());

    // keep SHARED here, see loop below
    TestKey tk0(KeySet::MAX_VERSION, SHARED, true, "a0");
    wso.append_key(tk0());
    fail_if (wso.is_empty());

    uint64_t const data_out_volatile(0xaabbccdd);
    uint32_t const data_out_persistent(0xffeeddcc);
    uint16_t const flag2(0x1234);

    {
        uint64_t const d(data_out_volatile);
        wso.append_data (&d, sizeof(d), true);
    }

    wso.append_data (&data_out_persistent, sizeof(data_out_persistent), false);
    wso.add_flags (flag2);

    uint16_t const flags(flag1 | flag2);

    WriteSetNG::GatherVector out;
    size_t const out_size(wso.gather(source, conn, trx, out));

    log_info << "Gather size: " << out_size << ", buf count: " << out->size();

    wsrep_seqno_t const last_seen(1);
    wsrep_seqno_t const seqno(2);
    int const           pa_range(seqno - last_seen);

    wso.set_last_seen(last_seen);

    /* concatenate all out buffers */
    std::vector<gu::byte_t> in;
    in.reserve(out_size);
    for (size_t i(0); i < out->size(); ++i)
    {
        const gu::byte_t* ptr(static_cast<const gu::byte_t*>(out[i].ptr));
        in.insert (in.end(), ptr, ptr + out[i].size);
    }

    fail_if (in.size() != out_size);

    gu::Buf const in_buf = { in.data(), static_cast<ssize_t>(in.size()) };

    /* read ws buffer and "certify" */
    {
        mark_point();
        WriteSetIn wsi(in_buf);

        mark_point();
        wsrep_seqno_t const ls(wsi.last_seen());
        fail_if (ls != last_seen, "Found last seen: %lld, expected: %lld",
                 ls, last_seen);
        fail_if (wsi.flags() != flags);
        fail_if (0 == wsi.timestamp());
        fail_if (wsi.annotated());

        mark_point();
        const KeySetIn& ksi(wsi.keyset());
        fail_if (ksi.count() != 1);

        mark_point();
        int shared(0);
        for (int i(0); i < ksi.count(); ++i)
        {
            KeySet::KeyPart kp(ksi.next());
            shared += kp.shared();
        }
        fail_unless(shared > 0);

        wsi.verify_checksum();

        wsi.set_seqno (seqno, pa_range);
        fail_unless(wsi.certified());
    }
    /* repeat reading buffer after "certification" */
    {
        WriteSetIn wsi(in_buf);
        fail_unless(wsi.certified());
        fail_if (wsi.seqno() != seqno);
        fail_if (wsi.flags() != flags);
        fail_if (0 == wsi.timestamp());

        mark_point();
        const KeySetIn& ksi(wsi.keyset());
        fail_if (ksi.count() != 1);

        mark_point();
        int shared(0);
        for (int i(0); i < ksi.count(); ++i)
        {
            KeySet::KeyPart kp(ksi.next());
            shared += kp.shared();
        }
        fail_unless(shared > 0);

        wsi.verify_checksum();

        mark_point();
        const DataSetIn& dsi(wsi.dataset());
        fail_if (dsi.count() != 1);

        mark_point();
        gu::Buf const d(dsi.next());
        fail_if (d.size !=
                 sizeof(data_out_volatile) + sizeof(data_out_persistent));

        const char* dptr = static_cast<const char*>(d.ptr);
        fail_if (*(reinterpret_cast<const uint64_t*>(dptr)) !=
                 data_out_volatile);
        fail_if (*(reinterpret_cast<const uint32_t*>
                                    (dptr + sizeof(data_out_volatile))) !=
                 data_out_persistent);

        mark_point();
        const DataSetIn& usi(wsi.unrdset());
        fail_if (usi.count() != 0);
        fail_if (usi.size()  != 0);
    }

    mark_point();

    try /* this is to test checksum after set_seqno() */
    {
        WriteSetIn wsi(in_buf);
        mark_point();
        wsi.verify_checksum();
        fail_unless(wsi.certified());
        fail_if (wsi.pa_range()   != pa_range);
        fail_if (wsi.seqno()      != seqno);
        fail_if (memcmp(&wsi.source_id(), &source, sizeof(source)));
        fail_if (wsi.conn_id()    != conn);
        fail_if (wsi.trx_id()     != trx);
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
    }

    mark_point();

    /* this is to test reassembly without keys and unordered data after gather()
     * + late initialization */
    try
    {
        WriteSetIn tmp_wsi(in_buf);
        WriteSetIn::GatherVector out;

        mark_point();
        gu_trace(tmp_wsi.gather(out, false, false)); // no keys or unrd

        /* concatenate all out buffers */
        std::vector<gu::byte_t> in;
        in.reserve(out_size);
        for (size_t i(0); i < out->size(); ++i)
        {
            const gu::byte_t* ptr
                (static_cast<const gu::byte_t*>(out[i].ptr));
            in.insert (in.end(), ptr, ptr + out[i].size);
        }

        mark_point();
        gu::Buf tmp_buf = { in.data(), static_cast<ssize_t>(in.size()) };

        WriteSetIn wsi;        // first - create an empty writeset
        wsi.read_buf(tmp_buf); // next  - initialize from buffer
        wsi.verify_checksum();
        fail_unless(wsi.certified());
        fail_if (wsi.pa_range()        != pa_range);
        fail_if (wsi.seqno()           != seqno);
        fail_if (wsi.keyset().count()  != 0);
        fail_if (wsi.dataset().count() == 0);
        fail_if (wsi.unrdset().count() != 0);
    }
    catch (gu::Exception& e)
    {
        fail_if (e.get_errno() != EINVAL);
    }

    in[in.size() - 1] ^= 1; // corrupted the last byte (payload)

    mark_point();

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

START_TEST (ver3_annotation)
{
    uint16_t const flag1(0xabcd);
    wsrep_uuid_t source;
    gu_uuid_generate (reinterpret_cast<gu_uuid_t*>(&source), NULL, 0);
    wsrep_conn_id_t const conn(652653);
    wsrep_trx_id_t const  trx(99994952);

    std::string const dir(".");
    wsrep_trx_id_t trx_id(1);

    WriteSetOut wso (dir, trx_id, KeySet::FLAT16, 0, 0, flag1,WriteSetNG::VER3);

    fail_unless (wso.is_empty());

    TestKey tk0(KeySet::MAX_VERSION, SHARED, true, "key0");
    wso.append_key(tk0());
    fail_if (wso.is_empty());

    uint64_t const data(0xaabbccdd);
    std::string const annotation("0xaabbccdd");
    uint16_t const flag2(0x1234);

    wso.append_data (&data, sizeof(data), true);
    wso.append_annotation (annotation.c_str(), annotation.size(), true);
    wso.add_flags (flag2);

    uint16_t const flags(flag1 | flag2);
    WriteSetNG::GatherVector out;
    size_t const out_size(wso.gather(source, conn, trx, out));

    log_info << "Gather size: " << out_size << ", buf count: " << out->size();

    wsrep_seqno_t const last_seen(1);
    wso.set_last_seen(last_seen);

    /* concatenate all out buffers */
    std::vector<gu::byte_t> in;
    in.reserve(out_size);
    for (size_t i(0); i < out->size(); ++i)
    {
        const gu::byte_t* ptr(static_cast<const gu::byte_t*>(out[i].ptr));
        in.insert (in.end(), ptr, ptr + out[i].size);
    }

    fail_if (in.size() != out_size);

    gu::Buf const in_buf = { in.data(), static_cast<ssize_t>(in.size()) };

    /* read buffer into WriteSetIn */
    mark_point();
    WriteSetIn wsi(in_buf);

    mark_point();
    wsrep_seqno_t const ls(wsi.last_seen());
    fail_if (ls != last_seen, "Found last seen: %lld, expected: %lld",
             ls, last_seen);
    fail_if (wsi.flags() != flags);
    fail_if (0 == wsi.timestamp());

    wsi.verify_checksum();
    fail_if (!wsi.annotated());

    /* check that annotation has survived */
    std::ostringstream os;
    wsi.write_annotation(os);
    std::string const res(os.str());

    fail_if(annotation.length() != res.length());
    fail_if(annotation != res);
}
END_TEST

Suite* write_set_ng_suite ()
{
    TCase* t = tcase_create ("WriteSet");
    tcase_add_test (t, ver3_basic);
    tcase_add_test (t, ver3_annotation);
    tcase_set_timeout(t, 60);

    Suite* s = suite_create ("WriteSet");
    suite_add_tcase (s, t);

    return s;
}
