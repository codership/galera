//
// Copyright (C) 2015-2019 Codership Oy <info@codership.com>
//

#include "replicator_smm.hpp" // ReplicatorSMM::InitConfig
#include "certification.hpp"
#include "trx_handle.hpp"
#include "key_os.hpp"
#include "GCache.hpp"
#include "gu_config.hpp"
#include "gu_inttypes.hpp"

#include <check.h>

namespace
{
    class TestEnv
    {
    public:

        TestEnv() :
            conf_   (),
            init_   (conf_),
            gcache_ (conf_, ".")
        { }

        ~TestEnv() { ::unlink(GCACHE_NAME.c_str()); }

        gu::Config&         conf()   { return conf_  ; }
        gcache::GCache&     gcache() { return gcache_; }
    private:

        static std::string const GCACHE_NAME;

        gu::Config                        conf_;

        struct Init
        {
            galera::ReplicatorSMM::InitConfig init_;

            Init(gu::Config& conf) : init_(conf, NULL, NULL)
            {
                conf.set("gcache.name", GCACHE_NAME);
                conf.set("gcache.size", "1M");
            }
        }                                 init_;
        gcache::GCache                    gcache_;
    };

    struct WSInfo
    {
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
        galera::Certification::TestResult result;
        const char data_ptr[24];
        size_t data_len;
    };
}

std::string const TestEnv::GCACHE_NAME = "cert.cache";

static
void run_wsinfo(const WSInfo* const wsi, size_t const nws, int const version)
{
    galera::TrxHandleMaster::Pool mp(
        sizeof(galera::TrxHandleMaster) + sizeof(galera::WriteSetOut),
        16, "certification_mp");
    galera::TrxHandleSlave::Pool sp(
        sizeof(galera::TrxHandleSlave), 16, "certification_sp");
    TestEnv env;
    galera::Certification cert(env.conf(), 0);

    cert.assign_initial_position(gu::GTID(), version);
    galera::TrxHandleMaster::Params const trx_params(
        "", version, galera::KeySet::MAX_VERSION);

    mark_point();

    for (size_t i(0); i < nws; ++i)
    {
        galera::TrxHandleMasterPtr trx(galera::TrxHandleMaster::New(
                                           mp,
                                           trx_params,
                                           wsi[i].uuid,
                                           wsi[i].conn_id,
                                           wsi[i].trx_id),
                                       galera::TrxHandleMasterDeleter());
        trx->set_flags(wsi[i].flags);
        trx->append_key(
            galera::KeyData(version,
                            wsi[i].key,
                            wsi[i].iov_len,
                            (wsi[i].shared ? WSREP_KEY_SHARED :
                             WSREP_KEY_EXCLUSIVE),
                            true));


        if (wsi[i].data_len)
        {
            trx->append_data(wsi[i].data_ptr, wsi[i].data_len,
                             WSREP_DATA_ORDERED, false);
        }

        galera::WriteSetNG::GatherVector out;
        size_t size(trx->write_set_out().gather(trx->source_id(),
                                                trx->conn_id(),
                                                trx->trx_id(),
                                                out));
        trx->finalize(wsi[i].last_seen_seqno);

        // serialize write set into gcache buffer
        gu::byte_t* buf(static_cast<gu::byte_t*>(env.gcache().malloc(size)));
        ck_assert(out.serialize(buf, size) == size);


        gcs_action act = {wsi[i].global_seqno,
                          wsi[i].local_seqno,
                          buf,
                          static_cast<int32_t>(size),
                          GCS_ACT_WRITESET};
        galera::TrxHandleSlavePtr ts(galera::TrxHandleSlave::New(false, sp),
                                     galera::TrxHandleSlaveDeleter());
        ck_assert(ts->unserialize<true>(act) == size);

        galera::Certification::TestResult result(cert.append_trx(ts));
        ck_assert_msg(result == wsi[i].result,
                      "g: %" PRId64 " res: %d exp: %d",
                      ts->global_seqno(), result, wsi[i].result);
        ck_assert_msg(ts->depends_seqno() == wsi[i].expected_depends_seqno,
                      "wsi: %zu g: %" PRId64 " ld: %" PRId64 " eld: %" PRId64,
                      i, ts->global_seqno(), ts->depends_seqno(),
                      wsi[i].expected_depends_seqno);
        cert.set_trx_committed(*ts);

        if (ts->nbo_end() && ts->ends_nbo() != WSREP_SEQNO_UNDEFINED)
        {
            cert.erase_nbo_ctx(ts->ends_nbo());
        }
    }
}


START_TEST(test_certification_trx_v3)
{

    const int version(3);
    using galera::Certification;
    using galera::TrxHandle;
    using galera::void_cast;

    // TRX certification rules:
    // *
    WSInfo wsi[] = {
        // 1 - 4: shared - shared
        // First four cases are shared keys, they should not collide or
        // generate dependency
        // 1: no dependencies
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          1, 1, 0, 0, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 2: no dependencies
        { { {1, } }, 1, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          2, 2, 0, 0, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK , {0}, 0},
        // 3: no dependencies
        { { {2, } }, 1, 3,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          3, 3, 0, 0, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 4: no dependencies
        { { {3, } }, 1, 4,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          4, 4, 0, 0, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 5: shared - exclusive
        // 5: depends on 4
        { { {2, } }, 1, 5,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, false,
          5, 5, 0, 4, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 6 - 8: exclusive - shared
        // 6: collides with 5
        { { {1, } }, 1, 6,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          6, 6, 4, -1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0},
        // 7: depends on 5
        { { {2, } }, 1, 7,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          7, 7, 4, 5, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 8: collides with 5
        { { {1, } }, 1, 8,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, true,
          8, 8, 4, -1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0},
        // 9 - 10: shared key shadows dependency to 5
        // 9: depends on 5
        { { {2, } }, 1, 9,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          9, 9, 0, 5, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 10: depends on 5
        { { {2, } }, 1, 10,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          10, 10, 6, 5, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 11 - 13: exclusive - shared - exclusive dependency
        { { {2, } }, 1, 11,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, false,
          11, 11, 10, 10, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        { { {2, } }, 1, 12,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, true,
          12, 12, 10, 11, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        { { {2, } }, 1, 13,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, false,
          13, 13, 10, 12, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 14: conflicts with 13
        { { {1, } }, 1, 14,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, false,
          14, 14, 12, -1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0}
    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    run_wsinfo(wsi, nws, version);

}
END_TEST

START_TEST(test_certification_trx_different_level_v3)
{
    const int version(3);
    using galera::Certification;
    using galera::TrxHandle;
    using galera::void_cast;

    //
    // Test the following cases:
    // 1) exclusive (k1, k2, k3) <-> exclusive (k1, k2) -> dependency
    // 2) exclusive (k1, k2) <-> exclusive (k1, k2, k3) -> conflict
    //
    WSInfo wsi[] = {
        // 1)
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, false,
          1, 1, 0, 0, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        { { {2, } }, 2, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, false,
          2, 2, 0, 1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // 2)
        { { {2, } }, 2, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, false,
          3, 3, 2, 2, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1} }, 3, false,
          4, 4, 2, -1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0}
    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    run_wsinfo(wsi, nws, version);
}
END_TEST

START_TEST(test_certification_toi_v3)
{

    const int version(3);
    using galera::Certification;
    using galera::TrxHandle;
    using galera::void_cast;

    // Note that only exclusive keys are used for TOI.
    // TRX - TOI and TOI - TOI matches:
    // * TOI should always depend on preceding write set
    // TOI - TRX matches:
    // * if coming from the same source, dependency
    // * if coming from different sources, conflict
    // TOI - TOI matches:
    // * always dependency
    WSInfo wsi[] = {
        // TOI
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1, false,
          1, 1, 0, 0,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // TOI 2 Depends on TOI 1
        { { {2, } }, 2, 2,
          { {void_cast("1"), 1}, }, 1, false,
          2, 2, 0, 1,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // Trx 3 from the same source depends on TOI 2
        { { {2, } }, 3, 3,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, false,
          3, 3, 2, 2,
          TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // Trx 4 from different source conflicts with 3
        { { {3, } }, 3, 3,
          { {void_cast("1"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, false,
          4, 4, 2, -1,
          TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0},
        // Non conflicting TOI 5 depends on 4
        { { {1, } }, 2, 2,
          { {void_cast("2"), 1}, }, 1, false,
          5, 5, 0, 4,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // Trx 6 from different source conflicts with TOI 5
        { { {3, } }, 3, 3,
          { {void_cast("2"), 1}, {void_cast("1"), 1}, {void_cast("1"), 1}}, 3, false,
          6, 6, 4, -1,
          TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0}
    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    run_wsinfo(wsi, nws, version);

}
END_TEST


START_TEST(test_certification_nbo)
{
    log_info << "START: test_certification_nbo";
    const int version(galera::WriteSetNG::VER5);
    using galera::Certification;
    using galera::TrxHandle;
    using galera::void_cast;

    // Non blocking operations with respect to TOI
    // NBO - TOI: Always conflict
    // TOI - NBO: Always dependency
    WSInfo wsi[] = {
        // 1 - 2: NBO(1) - TOI(2)
        // 1 - 3: NBO(1) - NBO(3)
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1, false,
          1, 1, 0, 0,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN,
          Certification::TEST_OK, {0}, 0},
        { { {1, } }, 2, 2,
          { {void_cast("1"), 1}, }, 1, false,
          2, 2, 0, -1,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_FAILED, {0}, 0},
        { { {1, } }, 3, 3,
          { {void_cast("1"), 1}, }, 1, false,
          3, 3, 0, -1,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN,
          Certification::TEST_FAILED, {0}, 0},
        // 4 - 5 no conflict, different key
        { { {1, } }, 4, 4,
          { {void_cast("2"), 1}, }, 1, false,
          4, 4, 0, 3,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        { { {2, } }, 5, 5,
          { {void_cast("2"), 1}, }, 1, false,
          5, 5, 0, 4,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN,
          Certification::TEST_OK, {0}, 0},
        // 6 ends the NBO with key 1
        // notice the same uuid, conn_id/trx_id as the first entry
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, }, 1, false,
          6, 6, 0, 5,
          TrxHandle::F_ISOLATION | TrxHandle::F_COMMIT,
          Certification::TEST_OK,
          {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
          24
        },
        // 7 should now succeed
        { { {1, } }, 7, 7,
          { {void_cast("1"), 1}, }, 1, false,
          7, 7, 0, 6,
          TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        // Complete seqno 5 to clean up
        { { {2, } }, 8, 8,
          { {void_cast("2"), 1}, }, 1, false,
          8, 8, 0, 7,
          TrxHandle::F_ISOLATION | TrxHandle::F_COMMIT,
          Certification::TEST_OK,
          {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0},
          24
        }
    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    run_wsinfo(wsi, nws, version);

    log_info << "END: test_certification_nbo";
}
END_TEST


START_TEST(test_certification_commit_fragment)
{
    const int version(3);
    using galera::Certification;
    using galera::TrxHandle;
    using galera::void_cast;

    WSInfo wsi[] = {
        // commit fragment vs commit fragment
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, true,
          1, 1, 0, 0, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT | TrxHandle::F_PA_UNSAFE,
          Certification::TEST_OK, {0}, 0},
        { { {2, } }, 2, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, true,
          2, 2, 0, 1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT | TrxHandle::F_PA_UNSAFE,
          Certification::TEST_OK, {0}, 0},

        // TOI vs commit fragment
        { { {2, } }, 2, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, false,
          3, 3, 2, 2, TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0},
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, true,
          4, 4, 2, -1, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT | TrxHandle::F_PA_UNSAFE,
          Certification::TEST_FAILED, {0}, 0},

        // commit fragment vs TOI
        { { {2, } }, 2, 2,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, true,
          5, 5, 3, 4, TrxHandle::F_BEGIN | TrxHandle::F_COMMIT | TrxHandle::F_PA_UNSAFE,
          Certification::TEST_OK, {0}, 0},
        { { {1, } }, 1, 1,
          { {void_cast("1"), 1}, {void_cast("1"), 1} }, 2, false,
          6, 6, 4, 5, TrxHandle::F_ISOLATION | TrxHandle::F_BEGIN | TrxHandle::F_COMMIT,
          Certification::TEST_OK, {0}, 0}

    };

    size_t nws(sizeof(wsi)/sizeof(wsi[0]));

    run_wsinfo(wsi, nws, version);
}
END_TEST


Suite* certification_suite()
{
    Suite* s(suite_create("certification"));
    TCase* t;

    t = tcase_create("certification_trx_v3");
    tcase_add_test(t, test_certification_trx_v3);
    suite_add_tcase(s, t);

    t = tcase_create("certification_toi_v3");
    tcase_add_test(t, test_certification_toi_v3);
    suite_add_tcase(s, t);

    t = tcase_create("certification_trx_different_level_v3");
    tcase_add_test(t, test_certification_trx_different_level_v3);
    suite_add_tcase(s, t);

    t = tcase_create("certification_toi_v3");
    tcase_add_test(t, test_certification_toi_v3);
    suite_add_tcase(s, t);

    t = tcase_create("certification_nbo");
    tcase_add_test(t, test_certification_nbo);
    suite_add_tcase(s, t);

    t = tcase_create("certification_commit_fragment");
    tcase_add_test(t, test_certification_commit_fragment);
    suite_add_tcase(s, t);

    return s;
}
