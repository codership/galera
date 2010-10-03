/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "serialization.hpp"
#include "write_set.hpp"
#include "mapped_buffer.hpp"
#include "gu_logger.hpp"
#include "certification.hpp"
#include "wsdb.cpp"
#include <cstdlib>

#include <check.h>

using namespace std;
using namespace gu;
using namespace galera;

START_TEST(test_query_sequence)
{
    StatementSequence qs;

    Statement q1("foo", 3), q2("foobar", 6);
    size_t q1s(serial_size(q1));
    size_t q2s(serial_size(q2));

    fail_unless(q1s == 19);
    fail_unless(q2s == 22);

    qs.push_back(q1);
    size_t s1 = serial_size<StatementSequence::const_iterator, uint16_t>(
        qs.begin(), qs.end());

    fail_unless(s1 == q1s + 2, "%zd <-> %zd", s1, q1s + 2);
    qs.push_back(q2);
    size_t s2 = serial_size<StatementSequence::const_iterator, uint32_t>(
        qs.begin(), qs.end());
    fail_unless(s2 == q1s + q2s + 4, "%zd <-> %zd", s2, q1s + q2s + 4);

    log_info << "1";
    gu::Buffer buf(s2);
    size_t ret = serialize<StatementSequence::const_iterator, uint32_t>(
        qs.begin(), qs.end(), &buf[0], buf.size(), 0);
    fail_unless(ret == buf.size());

    log_info << "2";
    StatementSequence qs2;

    size_t ret2 = unserialize<Statement, uint32_t>(
        &buf[0], buf.size(), 0, back_inserter(qs2));

    fail_unless(ret2 == buf.size());

}
END_TEST

START_TEST(test_write_set)
{
    WriteSet ws;

    const char* query1 = "select 0";
    size_t query1_len = strlen(query1);
    const char* query2 = "insert into foo";
    size_t query2_len = strlen(query2);

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

    log_info << "q0 " << serial_size(ws);
    ws.append_query(query1, query1_len);
    log_info << "q1 " << serial_size(ws);
    ws.append_query(query2, query2_len);
    log_info << "q2 " << serial_size(ws);

    log_info << "ws0 " << serial_size(ws);
    ws.append_row_key(dbtable1, dbtable1_len, key1, key1_len, WriteSet::A_INSERT);
    log_info << "ws1 " << serial_size(ws);
    ws.append_row_key(dbtable2, dbtable2_len, key2, key2_len, WriteSet::A_UPDATE);
    log_info << "ws2 " << serial_size(ws);

    fail_unless(ws.get_level() == WriteSet::L_STATEMENT);
    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << serial_size<uint32_t>(rbrbuf);
    log_info << "wsrbr " << serial_size(ws);

    fail_unless(ws.get_level() == WriteSet::L_DATA);

    gu::Buffer buf(serial_size(ws));

    serialize(ws, &buf[0], buf.size(), 0);

    size_t expected_size =
        4 // hdr
        + 4 // query sequence size
        + 16 + query1_len // query1
        + 16 + query2_len // query2
        + 4 // row key sequence size
        + 2 + 6 + 2 + 3 + 1 // key1
        + 2 + 6 + 2 + 4 + 1 // key2
        + 4 + 6; // rbr
    fail_unless(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                buf.size(), expected_size, serial_size(ws));


    WriteSet ws2;

    size_t ret = unserialize(&buf[0], buf.size(), 0, ws2);
    fail_unless(ret == expected_size);
    fail_unless(ws2.get_level() == ws.get_level());
    fail_unless(ws2.get_queries().size() == 2);
    for (size_t i = 0; i < 2; ++i)
    {
        fail_unless(ws.get_queries()[i].get_query() ==
                    ws2.get_queries()[i].get_query());
        const gu::Buffer& q(ws.get_queries()[i].get_query());
        log_info << string(&q[0], &q[0] + q.size());
    }

    RowKeySequence rks;
    ws.get_keys(rks);

    RowKeySequence rks2;
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
                           wss[i].rk, wss[i].rk_len, WriteSet::A_UPDATE);
        trx->flush(0);
        string data("foobardata");
        trx->append_data(data.c_str(), data.size());
        trx->set_last_seen_seqno(i);
        trx->set_flags(TrxHandle::F_COMMIT);
        trx->flush(0);
        const MappedBuffer& wscoll(trx->write_set_collection());

        TrxHandle* trx2(cert.create_trx(&wscoll[0], wscoll.size(),
                                         i + 1, i + 1));

        cert.append_trx(trx2);
    }

    TrxHandle* trx(cert.get_trx(n_ws));
    fail_unless(trx != 0);
    cert.set_trx_committed(trx);
    fail_unless(cert.get_safe_to_discard_seqno() == -1,
                "get_safe_to_discard_seqno() = %lld, expected -1",
                static_cast<long long>(cert.get_safe_to_discard_seqno()));

    trx = cert.get_trx(1);
    fail_unless(trx != 0);
    cert.set_trx_committed(trx);
    fail_unless(cert.get_safe_to_discard_seqno() == 0);

    trx = cert.get_trx(4);
    fail_unless(trx != 0);
    cert.set_trx_committed(trx);
    fail_unless(cert.get_safe_to_discard_seqno() == 0);

    cert.purge_trxs_upto(cert.get_safe_to_discard_seqno());

}
END_TEST

Suite* write_set_suite()
{
    Suite* s = suite_create("write_set");
    TCase* tc;

    tc = tcase_create("test_query_sequence");
    tcase_add_test(tc, test_query_sequence);
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
    return s;
}
