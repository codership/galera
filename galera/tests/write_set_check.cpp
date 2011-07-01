/*
 * Copyright (C) 2010-2011 Codership Oy <info@codership.com>
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

typedef std::vector<galera::KeyPart> KeyPartSequence;


START_TEST(test_key)
{

    const wsrep_key_t kiovec[3] = {
        {"k1",   2 },
        {"k2",   2 },
        {"key3", 4 }
    };

    galera::Key key(kiovec, 3);
    fail_unless(galera::serial_size(key) == 2 + 3 + 3 + 5, "%ld <-> %ld",
                galera::serial_size(key), 2 + 3 + 3 + 5);

    KeyPartSequence kp(key.key_parts<KeyPartSequence>());
    fail_unless(kp.size() == 3);

    gu::Buffer buf(galera::serial_size(key));
    galera::serialize(key, &buf[0], buf.size(), 0);
    galera::Key key2;
    galera::unserialize(&buf[0], buf.size(), 0, key2);
    fail_unless(key2 == key);
}
END_TEST


START_TEST(test_write_set)
{
    WriteSet ws;

    const wsrep_key_t key1[2] = {
        {void_cast("dbt\0t1"), 6},
        {void_cast("aaa")    , 3}
    };

    const wsrep_key_t key2[2] = {
        {void_cast("dbt\0t2"), 6},
        {void_cast("bbbb"), 4}
    };

    const char* rbr = "rbrbuf";
    size_t rbr_len = 6;

    log_info << "ws0 " << serial_size(ws);
    ws.append_key(galera::Key(key1, 2));
    log_info << "ws1 " << serial_size(ws);
    ws.append_key(galera::Key(key2, 2));
    log_info << "ws2 " << serial_size(ws);

    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << serial_size<uint32_t>(rbrbuf);
    log_info << "wsrbr " << serial_size(ws);

    gu::Buffer buf(serial_size(ws));

    serialize(ws, &buf[0], buf.size(), 0);

    size_t expected_size =
        4 // row key sequence size
        + 2 + 1 + 6 + 1 + 3 // key1
        + 2 + 1 + 6 + 1 + 4 // key2
        + 4 + 6; // rbr
    fail_unless(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                buf.size(), expected_size, serial_size(ws));


    WriteSet ws2;

    size_t ret = unserialize(&buf[0], buf.size(), 0, ws2);
    fail_unless(ret == expected_size);

    WriteSet::KeySequence rks;
    ws.get_keys(rks);

    WriteSet::KeySequence rks2;
    ws.get_keys(rks2);

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
    cert.assign_initial_position(0, 0);
    wsrep_uuid_t uuid = {{1, }};

    const wsrep_key_t wss[6][2] = {
        {{void_cast("foo"), strlen("foo")}, {void_cast("1"), 1}},
        {{void_cast("foo"), strlen("foo")}, {void_cast("2"), 1}},
        {{void_cast("foo"), strlen("foo")}, {void_cast("3"), 1}},
        {{void_cast("foo"), strlen("foo")}, {void_cast("1"), 1}},
        {{void_cast("foo"), strlen("foo")}, {void_cast("2"), 1}},
        {{void_cast("foo"), strlen("foo")}, {void_cast("3"), 1}}
    };

    const size_t n_ws(6);

    for (size_t i = 0; i < n_ws; ++i)
    {
        TrxHandle* trx(new TrxHandle(0, uuid, i, i + 1, true));
        trx->append_key(Key(wss[i], 2));
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


START_TEST(test_cert_hierarchical_v0)
{
    log_info << "test_cert_hierarchical_v1";
    struct wsinfo_ {
        wsrep_uuid_t    uuid;
        wsrep_conn_id_t conn_id;
        wsrep_trx_id_t  trx_id;
        wsrep_key_t     key[3];
        size_t          iov_len;
        wsrep_seqno_t   local_seqno;
        wsrep_seqno_t   global_seqno;
        wsrep_seqno_t   last_seen_seqno;
        wsrep_seqno_t   expected_last_depends_seqno;
        int             flags;
        Certification::TestResult result;
    } wsi[] = {
        // 1: no dependencies
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          1, 1, 0, 0, 0, Certification::TEST_OK},
        // 2: depends on 1 (partial match, same source)
        { { {1, } }, 1, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          2, 2, 0, 1, 0, Certification::TEST_OK},
        // 3: no dependencies
        { { {1, } }, 1, 3,
          { {void_cast("2"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          3, 3, 0, 0, 0, Certification::TEST_OK},
        // 4: depends on 2 (full match, same source)
        { { {1, } }, 1, 4,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          4, 4, 0, 2, 0, Certification::TEST_OK},
        // 5: conflicts with 4 (full match, different source)
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          5, 5, 0, -1, 0, Certification::TEST_FAILED},
        // 6: conflicts with 3 (partial match, different source)
        { { {2, } }, 1, 2,
          { {void_cast("2"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3,
          6, 6, 0, -1, 0, Certification::TEST_FAILED},
        // 7: depends on 6 (TO isolation)
        { { {1, } }, 1, 5,
          { {0, 0}, {0, 0}, {0, 0} }, 0,
          7, 7, 0, 6, 0, Certification::TEST_OK},
        // 8: depends on 7 (same source, TO isolation)
        { { {1, } }, 1, 3,
          { {void_cast("4"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          8, 8, 0, 7, 0, Certification::TEST_OK},
        // 9: depends on 8
        { { {1, } }, 1, 3,
          { {void_cast("4"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          9, 9, 8, 8, TrxHandle::F_ISOLATION, Certification::TEST_OK},
        // 10: conflicts with 9 (F_ISOLATION, same source)
        { { {1, } }, 1, 3,
          { {void_cast("4"), 1}, {void_cast("1"), 1}, {0, 0}}, 2,
          10, 10, 8, -1, 0, Certification::TEST_FAILED},

    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    galera::Certification cert;
    cert.assign_initial_position(0, 0);
    for (size_t i(0); i < nws; ++i)
    {
        TrxHandle* trx(new TrxHandle(0, wsi[i].uuid, wsi[i].conn_id,
                                     wsi[i].trx_id, false));
        trx->append_key(Key(wsi[i].key, wsi[i].iov_len));
        trx->set_last_seen_seqno(wsi[i].last_seen_seqno);
        trx->set_flags(trx->flags() | wsi[i].flags);
        trx->flush(0);
        trx->set_seqnos(wsi[i].local_seqno, wsi[i].global_seqno);
        Certification::TestResult result(cert.append_trx(trx));
        fail_unless(result == wsi[i].result, "g: %lld r: %d er: %d",
                    trx->global_seqno(), result, wsi[i].result);
        fail_unless(trx->last_depends_seqno() ==
                    wsi[i].expected_last_depends_seqno,
                    "g: %lld ld: %lld eld: %lld",
                    trx->global_seqno(),
                    trx->last_depends_seqno(),
                    wsi[i].expected_last_depends_seqno);
        trx->unref();
    }
}
END_TEST


START_TEST(test_cert_hierarchical_v1)
{
    log_info << "test_cert_hierarchical_v1";
    struct wsinfo_ {
        wsrep_uuid_t    uuid;
        wsrep_conn_id_t conn_id;
        wsrep_trx_id_t  trx_id;
        wsrep_key_t     key[3];
        size_t          iov_len;
        wsrep_seqno_t   local_seqno;
        wsrep_seqno_t   global_seqno;
        wsrep_seqno_t   last_seen_seqno;
        wsrep_seqno_t   expected_last_depends_seqno;
        int             flags;
        Certification::TestResult result;
    } wsi[] = {
        // 1 - 3, test symmetric case for dependencies
        // 1: no dependencies
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          1, 1, 0, 0, 0, Certification::TEST_OK},
        // 2: depends on 1, no conflict
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2,
          2, 2, 0, 1, 0, Certification::TEST_OK},
        // 3: depends on 2, no conflict
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          3, 3, 0, 2, 0, Certification::TEST_OK},
        // 4 - 8, test symmetric case for conflicts
        // 4: depends on 3, no conflict
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          4, 4, 3, 3, 0, Certification::TEST_OK},
        // 5: conflict with 4
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2,
          5, 5, 3, -1, 0, Certification::TEST_FAILED},
        // 6: depends on 4 (failed 5 not present in index), no conflict
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2,
          6, 6, 5, 4, 0, Certification::TEST_OK},
        // 7: conflicts with 6
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          7, 7, 5, -1, 0, Certification::TEST_FAILED},
        // 8: to isolation: must not conflict, depends on global_seqno - 1
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          8, 8, 5, 7, TrxHandle::F_ISOLATION, Certification::TEST_OK},
        // 9: to isolation: must not conflict, depends on global_seqno - 1
        { { {2, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1,
          9, 9, 5, 8, TrxHandle::F_ISOLATION, Certification::TEST_OK},


    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    galera::Certification cert;
    cert.assign_initial_position(0, 1);
    for (size_t i(0); i < nws; ++i)
    {
        TrxHandle* trx(new TrxHandle(1, wsi[i].uuid, wsi[i].conn_id,
                                     wsi[i].trx_id, false));
        trx->append_key(Key(wsi[i].key, wsi[i].iov_len));
        trx->set_last_seen_seqno(wsi[i].last_seen_seqno);
        trx->set_flags(trx->flags() | wsi[i].flags);
        trx->flush(0);
        trx->set_seqnos(wsi[i].local_seqno, wsi[i].global_seqno);
        Certification::TestResult result(cert.append_trx(trx));
        fail_unless(result == wsi[i].result, "g: %lld r: %d er: %d",
                    trx->global_seqno(), result, wsi[i].result);
        fail_unless(trx->last_depends_seqno() ==
                    wsi[i].expected_last_depends_seqno,
                    "g: %lld ld: %lld eld: %lld",
                    trx->global_seqno(),
                    trx->last_depends_seqno(),
                    wsi[i].expected_last_depends_seqno);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST


Suite* write_set_suite()
{
    Suite* s = suite_create("write_set");
    TCase* tc;

    tc = tcase_create("test_key");
    tcase_add_test(tc, test_key);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_set");
    tcase_add_test(tc, test_write_set);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_mapped_buffer");
    tcase_add_test(tc, test_mapped_buffer);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert");
    tcase_add_test(tc, test_cert);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_hierarchical_v0");
    tcase_add_test(tc, test_cert_hierarchical_v0);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_hierarchical_v1");
    tcase_add_test(tc, test_cert_hierarchical_v1);
    suite_add_tcase(s, tc);


    return s;
}
