/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "serialization.hpp"
#include "write_set.hpp"
#include "mapped_buffer.hpp"
#include "gu_logger.hpp"
#include "certification.hpp"
#include "wsdb.cpp"
#include "gcs_action_source.hpp"
#include <cstdlib>

#include <check.h>

using namespace std;
using namespace gu;
using namespace galera;

START_TEST(test_write_set)
{
    WriteSet ws;

    const char* dbtable1 = "dbt\0t1";
    size_t dbtable1_len = 6;
    const char* key1 = "aaa";
    size_t key1_len = 3;

    const char* dbtable2 = "dbt\0t2";
    size_t dbtable2_len = 6;
    const char* key2 = "bbbb";
    size_t key2_len = 4;

    const char* rbr = "rbrbuf";
    size_t rbr_len = 6;

    log_info << "ws0 " << serial_size(ws);
    ws.append_row_id(dbtable1, dbtable1_len, key1, key1_len);
    log_info << "ws1 " << serial_size(ws);
    ws.append_row_id(dbtable2, dbtable2_len, key2, key2_len);
    log_info << "ws2 " << serial_size(ws);

    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << serial_size<uint32_t>(rbrbuf);
    log_info << "wsrbr " << serial_size(ws);

    gu::Buffer buf(serial_size(ws));

    serialize(ws, &buf[0], buf.size(), 0);

    size_t expected_size =
        4 // row key sequence size
        + 2 + 6 + 2 + 3 // key1
        + 2 + 6 + 2 + 4 // key2
        + 4 + 6; // rbr
    fail_unless(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                buf.size(), expected_size, serial_size(ws));


    WriteSet ws2;

    size_t ret = unserialize(&buf[0], buf.size(), 0, ws2);
    fail_unless(ret == expected_size);

    RowIdSequence rks;
    ws.get_row_ids(rks);

    RowIdSequence rks2;
    ws.get_row_ids(rks2);

    fail_unless(rks2 == rks);

    fail_unless(ws2.get_data() == ws.get_data());

}
END_TEST


START_TEST(test_mapped_buffer)
{
    string wd("/tmp");
    MappedBuffer mb(wd, 1 << 8);

    mb.resize(16);
    for (size_t i = 0; i < 16; ++i)
    {
        mb[i] = static_cast<byte_t>(i);
    }

    mb.resize(1 << 8);
    for (size_t i = 0; i < 16; ++i)
    {
        fail_unless(mb[i] == static_cast<byte_t>(i));
    }

    for (size_t i = 16; i < (1 << 8); ++i)
    {
        mb[i] = static_cast<byte_t>(i);
    }

    mb.resize(1 << 20);

    for (size_t i = 0; i < (1 << 8); ++i)
    {
        fail_unless(mb[i] == static_cast<byte_t>(i));
    }

    for (size_t i = 0; i < (1 << 20); ++i)
    {
        mb[i] = static_cast<byte_t>(i);
    }

}
END_TEST


START_TEST(test_cert)
{
    Certification cert;
    cert.assign_initial_position(0);
    wsrep_uuid_t uuid = {{1, }};

    struct ws_
    {
        const char* dbtable;
        const size_t dbtable_len;
        const char* rk;
        const size_t rk_len;
    } wss[] = {
        {"foo", strlen("foo"), "1", 1},
        {"foo", strlen("foo"), "2", 1},
        {"foo", strlen("foo"), "3", 1},
        {"foo", strlen("foo"), "1", 1},
        {"foo", strlen("foo"), "2", 1},
        {"foo", strlen("foo"), "3", 1}
    };

    const size_t n_ws(sizeof(wss)/sizeof(wss[0]));

    for (size_t i = 0; i < n_ws; ++i)
    {
        TrxHandle* trx(new TrxHandle(uuid, i, i + 1, true));
        trx->append_row_id(wss[i].dbtable, wss[i].dbtable_len,
                           wss[i].rk, wss[i].rk_len);
        trx->flush(0);
        string data("foobardata");
        trx->append_data(data.c_str(), data.size());
        trx->set_last_seen_seqno(i);
        trx->set_flags(TrxHandle::F_COMMIT);
        trx->flush(0);
        const MappedBuffer& wscoll(trx->write_set_collection());


        GcsActionTrx trx2(&wscoll[0], wscoll.size(), i + 1, i + 1);
        cert.append_trx(trx2.trx());
        trx->unref();
    }

    TrxHandle* trx(cert.get_trx(n_ws));
    fail_unless(trx != 0);
    cert.set_trx_committed(trx);
    fail_unless(cert.get_safe_to_discard_seqno() == -1,
                "get_safe_to_discard_seqno() = %lld, expected -1",
                static_cast<long long>(cert.get_safe_to_discard_seqno()));
    trx->unref();

    trx = cert.get_trx(1);
    fail_unless(trx != 0);
    cert.set_trx_committed(trx);
    fail_unless(cert.get_safe_to_discard_seqno() == 0);
    trx->unref();

    trx = cert.get_trx(4);
    fail_unless(trx != 0);
    cert.set_trx_committed(trx);
    fail_unless(cert.get_safe_to_discard_seqno() == 0);
    trx->unref();

    cert.purge_trxs_upto(cert.get_safe_to_discard_seqno());

}
END_TEST


START_TEST(test_cert_iso)
{
    Certification cert;
    cert.assign_initial_position(0);
    wsrep_uuid_t uuid = {{1, }};


    TrxHandle* trx(new TrxHandle(uuid, 0, 1, true));
    trx->append_row_id("foo", strlen("foo"), "1", 1);
    trx->set_last_seen_seqno(0);
    trx->set_flags(TrxHandle::F_COMMIT);
    trx->flush(0);
    trx->set_seqnos(0, 1);
    Certification::TestResult tres(cert.append_trx(trx));
    fail_unless(tres == Certification::TEST_OK);
    fail_unless(trx->last_depends_seqno() == 0);
    trx->unref();

    trx = new TrxHandle(uuid, 1, -1, true);
    trx->set_last_seen_seqno(0);
    trx->set_flags(TrxHandle::F_COMMIT);
    trx->flush(0);
    trx->set_seqnos(1, 2);
    tres = cert.append_trx(trx);
    fail_unless(tres == Certification::TEST_OK);
    fail_unless(trx->last_depends_seqno() == 1,
                "expected last depends seqno 1, got %lld",
                trx->last_depends_seqno());
    trx->unref();

    trx = new TrxHandle(uuid, 2, 3, true);
    trx->append_row_id("foo", strlen("foo"), "3", 1);
    trx->set_last_seen_seqno(0);
    trx->set_flags(TrxHandle::F_COMMIT);
    trx->flush(0);
    trx->set_seqnos(2, 3);
    tres = cert.append_trx(trx);
    fail_unless(tres == Certification::TEST_OK);
    fail_unless(trx->last_depends_seqno() == 2);
    trx->unref();

    trx = cert.get_trx(2);
    cert.set_trx_committed(trx);
    fail_unless(trx->is_committed() == true);
    trx->unref();

    trx = new TrxHandle(uuid, 3, 4, true);
    trx->append_row_id("foo", strlen("foo"), "1", 1);
    trx->set_last_seen_seqno(0);
    trx->set_flags(TrxHandle::F_COMMIT);
    trx->flush(0);
    trx->set_seqnos(3, 4);
    tres = cert.append_trx(trx);
    fail_unless(tres == Certification::TEST_OK);
    fail_unless(trx->last_depends_seqno() == 1,
                "expected last depends seqno 1, got %lld",
                trx->last_depends_seqno());
    trx->unref();

}
END_TEST

Suite* write_set_suite()
{
    Suite* s = suite_create("write_set");
    TCase* tc;

    tc = tcase_create("test_write_set");
    tcase_add_test(tc, test_write_set);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_mapped_buffer");
    tcase_add_test(tc, test_mapped_buffer);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert");
    tcase_add_test(tc, test_cert);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_iso");
    tcase_add_test(tc, test_cert_iso);
    suite_add_tcase(s, tc);

    return s;
}
