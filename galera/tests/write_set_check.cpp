/*
 * Copyright (C) 2010-2020 Codership Oy <info@codership.com>
 */

#include "write_set.hpp"
#include "mapped_buffer.hpp"
#include "gu_logger.hpp"
#include "certification.hpp"
#include "replicator_smm.hpp"
#include "wsdb.cpp"
#include "gcs_action_source.hpp"
#include "galera_service_thd.hpp"

#include "gu_inttypes.hpp"

#include <cstdlib>
#include <check.h>

namespace
{
    class TestEnv
    {
        class GCache_setup
        {
        public:
            GCache_setup(gu::Config& conf) : name_("write_set_test.gcache")
            {
                conf.set("gcache.name", name_);
                conf.set("gcache.size", "1M");
                log_info << "conf for gcache: " << conf;
            }

            ~GCache_setup()
            {
                unlink(name_.c_str());
            }
        private:
            std::string const name_;
        };

    public:

        TestEnv() :
            conf_   (),
            init_   (conf_, NULL, NULL),
            gcache_setup_(conf_),
            gcache_ (conf_, "."),
            gcs_    (conf_, gcache_),
            thd_    (gcs_,  gcache_)
        {}

        ~TestEnv() {}

        gu::Config&         conf() { return conf_; }
        galera::ServiceThd& thd()  { return thd_;  }

    private:

        gu::Config         conf_;
        galera::ReplicatorSMM::InitConfig init_;
        GCache_setup       gcache_setup_;
        gcache::GCache     gcache_;
        galera::DummyGcs   gcs_;
        galera::ServiceThd thd_;
    };
}

using namespace std;
using namespace galera;

typedef std::vector<galera::KeyPartOS> KeyPartSequence;

START_TEST(test_key1)
{
    static char k1[16];
    static char k2[256];
    static char k3[1 << 21];
    static char k4[(1 << 22) - 5];

    memset(k1, 0xab, sizeof(k1));
    memset(k2, 0xcd, sizeof(k2));
    memset(k3, 0x9e, sizeof(k3));
    memset(k4, 0x8f, sizeof(k4));

    const wsrep_buf_t kiovec[4] = {
        {k1, sizeof k1 },
        {k2, sizeof k2 },
        {k3, sizeof k3 },
        {k4, sizeof k4 }
    };

    KeyOS key(1, kiovec, 4, 0);
    size_t expected_size(0);

#ifndef GALERA_KEY_VLQ
    expected_size += 1 + std::min(sizeof k1, size_t(0xff));
    expected_size += 1 + std::min(sizeof k2, size_t(0xff));
    expected_size += 1 + std::min(sizeof k3, size_t(0xff));
    expected_size += 1 + std::min(sizeof k4, size_t(0xff));
    expected_size += sizeof(uint16_t);
#else
    expected_size += gu::uleb128_size(sizeof k1) + sizeof k1;
    expected_size += gu::uleb128_size(sizeof k2) + sizeof k2;
    expected_size += gu::uleb128_size(sizeof k3) + sizeof k3;
    expected_size += gu::uleb128_size(sizeof k4) + sizeof k4;
    expected_size += gu::uleb128_size(expected_size);
#endif

    ck_assert_msg(key.serial_size() == expected_size, "%ld <-> %ld",
                  key.serial_size(), expected_size);

    KeyPartSequence kp(key.key_parts<KeyPartSequence>());
    ck_assert(kp.size() == 4);

    gu::Buffer buf(key.serial_size());
    key.serialize(&buf[0], buf.size(), 0);
    KeyOS key2(1);
    key2.unserialize(&buf[0], buf.size(), 0);
    ck_assert(key2 == key);
}
END_TEST


START_TEST(test_key2)
{
    static char k1[16];
    static char k2[256];
    static char k3[1 << 21];
    static char k4[(1 << 22) - 5];

    memset(k1, 0xab, sizeof(k1));
    memset(k2, 0xcd, sizeof(k2));
    memset(k3, 0x9e, sizeof(k3));
    memset(k4, 0x8f, sizeof(k4));

    const wsrep_buf_t kiovec[4] = {
        {k1, sizeof k1 },
        {k2, sizeof k2 },
        {k3, sizeof k3 },
        {k4, sizeof k4 }
    };

    KeyOS key(2, kiovec, 4, 0);
    size_t expected_size(0);

    expected_size += 1; // flags
#ifndef GALERA_KEY_VLQ
    expected_size += 1 + std::min(sizeof k1, size_t(0xff));
    expected_size += 1 + std::min(sizeof k2, size_t(0xff));
    expected_size += 1 + std::min(sizeof k3, size_t(0xff));
    expected_size += 1 + std::min(sizeof k4, size_t(0xff));
    expected_size += sizeof(uint16_t);
#else
    expected_size += gu::uleb128_size(sizeof k1) + sizeof k1;
    expected_size += gu::uleb128_size(sizeof k2) + sizeof k2;
    expected_size += gu::uleb128_size(sizeof k3) + sizeof k3;
    expected_size += gu::uleb128_size(sizeof k4) + sizeof k4;
    expected_size += gu::uleb128_size(expected_size);
#endif

    ck_assert_msg(key.serial_size() == expected_size, "%ld <-> %ld",
                  key.serial_size(), expected_size);

    KeyPartSequence kp(key.key_parts<KeyPartSequence>());
    ck_assert(kp.size() == 4);

    gu::Buffer buf(key.serial_size());
    key.serialize(&buf[0], buf.size(), 0);
    KeyOS key2(2);
    key2.unserialize(&buf[0], buf.size(), 0);
    ck_assert(key2 == key);
}
END_TEST


START_TEST(test_write_set1)
{
    WriteSet ws(1);

    const wsrep_buf_t key1[2] = {
        {void_cast("dbt\0t1"), 6},
        {void_cast("aaa")    , 3}
    };

    const wsrep_buf_t key2[2] = {
        {void_cast("dbt\0t2"), 6},
        {void_cast("bbbb"), 4}
    };

    const char* rbr = "rbrbuf";
    size_t rbr_len = 6;

    log_info << "ws0 " << ws.serial_size();
    ws.append_key(KeyData(1, key1, 2, WSREP_KEY_EXCLUSIVE, true));
    log_info << "ws1 " << ws.serial_size();
    ws.append_key(KeyData(1, key2, 2, WSREP_KEY_EXCLUSIVE, true));
    log_info << "ws2 " << ws.serial_size();

    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << gu::serial_size4(rbrbuf);
    log_info << "wsrbr " << ws.serial_size();

    gu::Buffer buf(ws.serial_size());

    ws.serialize(&buf[0], buf.size(), 0);

    size_t expected_size =
        4 // row key sequence size
#ifndef GALERA_KEY_VLQ
        + 2 + 1 + 6 + 1 + 3 // key1
        + 2 + 1 + 6 + 1 + 4 // key2
#else
        + 1 + 1 + 6 + 1 + 3 // key1
        + 1 + 1 + 6 + 1 + 4 // key2
#endif
        + 4 + 6; // rbr
    ck_assert_msg(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                  buf.size(), expected_size, ws.serial_size());


    WriteSet ws2(0);

    size_t ret = ws2.unserialize(&buf[0], buf.size(), 0);
    ck_assert(ret == expected_size);

    WriteSet::KeySequence rks;
    ws.get_keys(rks);

    WriteSet::KeySequence rks2;
    ws.get_keys(rks2);

    ck_assert(rks2 == rks);

    ck_assert(ws2.get_data() == ws.get_data());

}
END_TEST


START_TEST(test_write_set2)
{
    WriteSet ws(2);

    const wsrep_buf_t key1[2] = {
        {void_cast("dbt\0t1"), 6},
        {void_cast("aaa")    , 3}
    };

    const wsrep_buf_t key2[2] = {
        {void_cast("dbt\0t2"), 6},
        {void_cast("bbbb"), 4}
    };

    const char* rbr = "rbrbuf";
    size_t rbr_len = 6;

    log_info << "ws0 " << ws.serial_size();
    ws.append_key(KeyData(2, key1, 2, WSREP_KEY_EXCLUSIVE, true));
    log_info << "ws1 " << ws.serial_size();
    ws.append_key(KeyData(2, key2, 2, WSREP_KEY_EXCLUSIVE, true));
    log_info << "ws2 " << ws.serial_size();

    ws.append_data(rbr, rbr_len);

    gu::Buffer rbrbuf(rbr, rbr + rbr_len);
    log_info << "rbrlen " << gu::serial_size4(rbrbuf);
    log_info << "wsrbr " << ws.serial_size();

    gu::Buffer buf(ws.serial_size());

    ws.serialize(&buf[0], buf.size(), 0);

    size_t expected_size =
        4 // row key sequence size
#ifndef GALERA_KEY_VLQ
        + 2 + 1 + 1 + 6 + 1 + 3 // key1
        + 2 + 1 + 1 + 6 + 1 + 4 // key2
#else
        + 1 + 1 + 6 + 1 + 3 // key1
        + 1 + 1 + 6 + 1 + 4 // key2
#endif
        + 4 + 6; // rbr
    ck_assert_msg(buf.size() == expected_size, "%zd <-> %zd <-> %zd",
                  buf.size(), expected_size, ws.serial_size());


    WriteSet ws2(2);

    size_t ret = ws2.unserialize(&buf[0], buf.size(), 0);
    ck_assert(ret == expected_size);

    WriteSet::KeySequence rks;
    ws.get_keys(rks);

    WriteSet::KeySequence rks2;
    ws2.get_keys(rks2);

    ck_assert(rks2 == rks);

    ck_assert(ws2.get_data() == ws.get_data());

}
END_TEST


START_TEST(test_mapped_buffer)
{
    string wd("/tmp");
    MappedBuffer mb(wd, 1 << 8);

    mb.resize(16);
    for (size_t i = 0; i < 16; ++i)
    {
        mb[i] = static_cast<gu::byte_t>(i);
    }

    mb.resize(1 << 8);
    for (size_t i = 0; i < 16; ++i)
    {
        ck_assert(mb[i] == static_cast<gu::byte_t>(i));
    }

    for (size_t i = 16; i < (1 << 8); ++i)
    {
        mb[i] = static_cast<gu::byte_t>(i);
    }

    mb.resize(1 << 20);

    for (size_t i = 0; i < (1 << 8); ++i)
    {
        ck_assert(mb[i] == static_cast<gu::byte_t>(i));
    }

    for (size_t i = 0; i < (1 << 20); ++i)
    {
        mb[i] = static_cast<gu::byte_t>(i);
    }

}
END_TEST

static TrxHandle::LocalPool
lp(TrxHandle::LOCAL_STORAGE_SIZE(), 4, "ws_local_pool");

static TrxHandle::SlavePool
sp(sizeof(TrxHandle), 4, "ws_slave_pool");

START_TEST(test_cert_hierarchical_v1)
{
    log_info << "test_cert_hierarchical_v1";

    struct wsinfo_ {
        wsrep_uuid_t     uuid;
        wsrep_conn_id_t  conn_id;
        wsrep_trx_id_t   trx_id;
        wsrep_buf_t      key[3];
        size_t           iov_len;
        wsrep_seqno_t    local_seqno;
        wsrep_seqno_t    global_seqno;
        wsrep_seqno_t    last_seen_seqno;
        wsrep_seqno_t    expected_depends_seqno;
        int              flags;
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

    TestEnv env;
    galera::Certification cert(env.conf(), env.thd());
    int const version(1);
    cert.assign_initial_position(0, version);
    galera::TrxHandle::Params const trx_params("", version,KeySet::MAX_VERSION);

    mark_point();

    for (size_t i(0); i < nws; ++i)
    {
        TrxHandle* trx(TrxHandle::New(lp, trx_params, wsi[i].uuid,
                                     wsi[i].conn_id, wsi[i].trx_id));
        trx->append_key(KeyData(1, wsi[i].key, wsi[i].iov_len,
                                WSREP_KEY_EXCLUSIVE, true));
        trx->set_last_seen_seqno(wsi[i].last_seen_seqno);
        trx->set_flags(trx->flags() | wsi[i].flags);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = TrxHandle::New(sp);
        size_t offset(trx->unserialize(&buf[0], buf.size(), 0));
        log_info << "ws[" << i << "]: " << buf.size() - offset;
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, wsi[i].local_seqno, wsi[i].global_seqno);
        Certification::TestResult result(cert.append_trx(trx));
        ck_assert_msg(result == wsi[i].result,
                      "wsi: %zu, g: %" PRId64 " r: %d er: %d",
                      i, trx->global_seqno(), result, wsi[i].result);
        ck_assert_msg(trx->depends_seqno() == wsi[i].expected_depends_seqno,
                      "wsi: %zu g: %" PRId64 " ld: %" PRId64 " eld: %" PRId64,
                      i, trx->global_seqno(), trx->depends_seqno(),
                      wsi[i].expected_depends_seqno);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST


START_TEST(test_cert_hierarchical_v2)
{
    log_info << "test_cert_hierarchical_v2";
    const int version(2);
    struct wsinfo_ {
        wsrep_uuid_t     uuid;
        wsrep_conn_id_t  conn_id;
        wsrep_trx_id_t   trx_id;
        wsrep_buf_t      key[3];
        size_t           iov_len;
        bool             shared;
        wsrep_seqno_t    local_seqno;
        wsrep_seqno_t    global_seqno;
        wsrep_seqno_t    last_seen_seqno;
        wsrep_seqno_t    expected_depends_seqno;
        int              flags;
        Certification::TestResult result;
    } wsi[] = {
        // 1 - 4: shared - shared
        // First four cases are shared keys, they should not collide or
        // generate dependency
        // 1: no dependencies
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1, true,
          1, 1, 0, 0, 0, Certification::TEST_OK},
        // 2: no dependencies
        { { {1, } }, 1, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          2, 2, 0, 0, 0, Certification::TEST_OK},
        // 3: no dependencies
        { { {2, } }, 1, 3,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          3, 3, 0, 0, 0, Certification::TEST_OK},
        // 4: no dependencies
        { { {3, } }, 1, 4,
          { {void_cast("1"), 1}, }, 1, true,
          4, 4, 0, 0, 0, Certification::TEST_OK},
        // 5: shared - exclusive
        // 5: depends on 4
        { { {2, } }, 1, 5,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, false,
          5, 5, 0, 4, 0, Certification::TEST_OK},
        // 6 - 8: exclusive - shared
        // 6: collides with 5
        { { {1, } }, 1, 6,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          6, 6, 4, -1, 0, Certification::TEST_FAILED},
        // 7: collides with 5
        { { {1, } }, 1, 7,
          { {void_cast("1"), 1}, }, 1, true,
          7, 7, 4, -1, 0, Certification::TEST_FAILED},
        // 8: collides with 5
        { { {1, } }, 1, 8,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, true,
          8, 8, 4, -1, 0, Certification::TEST_FAILED},
        // 9 - 10: shared key shadows dependency to 5
        // 9: depends on 5
        { { {2, } }, 1, 9,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          9, 9, 0, 5, 0, Certification::TEST_OK},
        // 10: depends on 5
        { { {2, } }, 1, 10,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          10, 10, 6, 5, 0, Certification::TEST_OK},
        // 11 - 13: exclusive - shared - exclusive dependency
        { { {2, } }, 1, 11,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, false,
          11, 11, 10, 10, 0, Certification::TEST_OK},
        { { {2, } }, 1, 12,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, true,
          12, 12, 10, 11, 0, Certification::TEST_OK},
        { { {2, } }, 1, 13,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, }, 2, false,
          13, 13, 10, 12, 0, Certification::TEST_OK},

    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    TestEnv env;
    galera::Certification cert(env.conf(), env.thd());

    cert.assign_initial_position(0, version);
    galera::TrxHandle::Params const trx_params("", version,KeySet::MAX_VERSION);

    mark_point();

    for (size_t i(0); i < nws; ++i)
    {
        TrxHandle* trx(TrxHandle::New(lp, trx_params, wsi[i].uuid,
                                      wsi[i].conn_id, wsi[i].trx_id));
        trx->append_key(KeyData(version, wsi[i].key, wsi[i].iov_len,
                                (wsi[i].shared ?
                                 WSREP_KEY_SHARED : WSREP_KEY_EXCLUSIVE),
                                true));
        trx->set_last_seen_seqno(wsi[i].last_seen_seqno);
        trx->set_flags(trx->flags() | wsi[i].flags);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = TrxHandle::New(sp);
        size_t offset(trx->unserialize(&buf[0], buf.size(), 0));
        log_info << "ws[" << i << "]: " << buf.size() - offset;
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, wsi[i].local_seqno, wsi[i].global_seqno);
        Certification::TestResult result(cert.append_trx(trx));
        ck_assert_msg(result == wsi[i].result, "g: %" PRId64 " res: %d exp: %d",
                      trx->global_seqno(), result, wsi[i].result);
        ck_assert_msg(trx->depends_seqno() == wsi[i].expected_depends_seqno,
                      "wsi: %zu g: %" PRId64 " ld: %" PRId64 " eld: %" PRId64,
                      i, trx->global_seqno(), trx->depends_seqno(),
                      wsi[i].expected_depends_seqno);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST

// This test leaks memory and it is for trx protocol version 2
// which is pre 25.3.5. Disabling this test for now with ASAN
// build. The test should be either removed or fixed to work
// with more recent protocol versions.
#ifndef GALERA_WITH_ASAN
START_TEST(test_trac_726)
{
    log_info << "test_trac_726";

    const int version(2);
    TestEnv env;
    galera::Certification cert(env.conf(), env.thd());
    galera::TrxHandle::Params const trx_params("", version,KeySet::MAX_VERSION);
    wsrep_uuid_t uuid1 = {{1, }};
    wsrep_uuid_t uuid2 = {{2, }};
    cert.assign_initial_position(0, version);

    mark_point();

    wsrep_buf_t key1 = {void_cast("1"), 1};
    wsrep_buf_t key2 = {void_cast("2"), 1};

    {
        TrxHandle* trx(TrxHandle::New(lp, trx_params, uuid1, 0, 0));

        trx->append_key(KeyData(version, &key1, 1, WSREP_KEY_EXCLUSIVE, true));
        trx->set_last_seen_seqno(0);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = TrxHandle::New(sp);
        size_t offset(trx->unserialize(&buf[0], buf.size(), 0));
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, 1, 1);
        Certification::TestResult result(cert.append_trx(trx));
        ck_assert(result == Certification::TEST_OK);
        cert.set_trx_committed(trx);
        trx->unref();
    }

    {
        TrxHandle* trx(TrxHandle::New(lp, trx_params, uuid2, 0, 0));

        trx->append_key(KeyData(version, &key2, 1, WSREP_KEY_EXCLUSIVE, true));
        trx->append_key(KeyData(version, &key2, 1, WSREP_KEY_SHARED,    true));
        trx->append_key(KeyData(version, &key1, 1, WSREP_KEY_EXCLUSIVE, true));

        trx->set_last_seen_seqno(0);
        trx->flush(0);

        // serialize/unserialize to verify that ver1 trx is serializable
        const galera::MappedBuffer& wc(trx->write_set_collection());
        gu::Buffer buf(wc.size());
        std::copy(&wc[0], &wc[0] + wc.size(), &buf[0]);
        trx->unref();
        trx = TrxHandle::New(sp);
        size_t offset(trx->unserialize(&buf[0], buf.size(), 0));
        trx->append_write_set(&buf[0] + offset, buf.size() - offset);

        trx->set_received(0, 2, 2);
        Certification::TestResult result(cert.append_trx(trx));
        ck_assert(result == Certification::TEST_FAILED);
        cert.set_trx_committed(trx);
        trx->unref();
    }
}
END_TEST
#endif // GALERA_WITH_ASAN

Suite* write_set_suite()
{
    Suite* s = suite_create("write_set");
    TCase* tc;

    tc = tcase_create("test_key1");
    tcase_add_test(tc, test_key1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_key2");
    tcase_add_test(tc, test_key2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_set1");
    tcase_add_test(tc, test_write_set1);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_set2");
    tcase_add_test(tc, test_write_set2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_mapped_buffer");
    tcase_add_test(tc, test_mapped_buffer);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_hierarchical_v1");
    tcase_add_test(tc, test_cert_hierarchical_v1);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_cert_hierarchical_v2");
    tcase_add_test(tc, test_cert_hierarchical_v2);
    tcase_set_timeout(tc, 120);
    suite_add_tcase(s, tc);

#ifndef GALERA_WITH_ASAN
    tc = tcase_create("test_trac_726");
    tcase_add_test(tc, test_trac_726);
    tcase_set_timeout(tc, 20);
    suite_add_tcase(s, tc);
#endif

    return s;
}
