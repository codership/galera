//
// Copyright (C) 2010-2020 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include <gu_uuid.hpp>

#include <vector>

#include <check.h>

using namespace std;
using namespace galera;


template <class T>
void check_states_graph(
    int  graph[TrxHandle::num_states_][TrxHandle::num_states_],
    T*   trx,
    const std::vector<int>& visits)
{
    // Check that no allowed state transition causes an abort
    std::vector<int> visited(TrxHandle::num_states_);
    std::fill(visited.begin(), visited.end(), 0);

    for (int i(0); i < TrxHandle::num_states_; ++i)
    {
        trx->force_state(TrxHandle::State(i));
        for (int j(0); j < TrxHandle::num_states_; ++j)
        {
            if (graph[i][j]){
                log_info << "Checking transition "
                         << trx->state()
                         << " -> "
                         << TrxHandle::State(j);
                trx->set_state(TrxHandle::State(j));
                visited[i] = 1;
                visited[j] = 1;
            }
            else
            {
                // TODO: Currently FSM transition calls abort on
                // unknown transition, figure out how to fix it
                // to verify also that incorrect transitions cause
                // proper error.
            }
            trx->force_state(TrxHandle::State(i));
        }
    }

    for (int i(0); i < TrxHandle::num_states_; ++i)
    {
        ck_assert_msg(visited[i] == visits[i],
                    "i = %i visited = %i visits = %i",
                    i, visited[i], visits[i]);
    }
}

START_TEST(test_states_master)
{
    log_info << "START test_states_master";
    TrxHandleMaster::Pool tp(TrxHandleMaster::LOCAL_STORAGE_SIZE(), 16,
                             "test_states_master");

    wsrep_uuid_t uuid = {{1, }};

    // first check basic stuff
    // 1) initial state is executing
    // 2) invalid state changes are caught
    // 3) valid state changes change state
    TrxHandleMasterPtr trx(TrxHandleMaster::New(tp, TrxHandleMaster::Defaults,
                                                uuid, -1, 1),
                           TrxHandleMasterDeleter());
    galera::TrxHandleLock lock(*trx);

    ck_assert(trx->state() == TrxHandle::S_EXECUTING);

    // Matrix representing directed graph of TrxHandleMaster transitions,
    // see galera/src/trx_handle.cpp

    // EXECUTING 0
    // MUST_ABORT 1
    // ABORTING 2
    // REPLICATING 3
    // CERTIFYING 4
    // MUST_REPLAY  5
    // REPLAYING 6
    // APPLYING 7
    // COMMITTING 8
    // ROLLING_BACK 9
    // COMMITTED 10
    // ROLLED_BACK 11

    int state_trans_master[TrxHandle::num_states_][TrxHandle::num_states_] = {
        // 0  1  2  3  4  5  6  7  8  9  10 11  To / From
        {  0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1 }, // 0
        {  0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 1
        {  1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1 }, // 2
        {  0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, // 3
        {  0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0 }, // 4
        {  0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0 }, // 5
        {  0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 }, // 6
        {  0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 }, // 7
        {  0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, // 8
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }, // 9
        {  1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 10
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }  // 11
    };

    // Visits all states
    std::vector<int> visits(TrxHandle::num_states_);
    std::fill(visits.begin(), visits.end(), 1);

    check_states_graph(state_trans_master, trx.get(), visits);
}
END_TEST

START_TEST(test_states_slave)
{
    log_info << "START test_states_slave";
    TrxHandleSlave::Pool  sp(sizeof(TrxHandleSlave), 16, "test_states_slave");
    int state_trans_slave[TrxHandle::num_states_][TrxHandle::num_states_] = {

        // 0  1  2  3  4  5  6  7  8  9  10 11  To / From
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 0  EXECUTING
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 1  MUST_ABORT
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 2  ABORTING
        {  0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, // 3  REPLICATING
        {  0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 }, // 4  CERTIFYING
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 5  MUST_REPLAY
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 6  REPLAYING
        {  0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 }, // 7  APPLYING
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }, // 8  COMMITTNG
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 9  ROLLING_BACK
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // 10 COMMITTED
        {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }  // 11 ROLLED_BACK
    };

    TrxHandleSlavePtr ts(TrxHandleSlave::New(false, sp),
                         TrxHandleSlaveDeleter());
    ck_assert(ts->state() == TrxHandle::S_REPLICATING);

    std::vector<int> visits(TrxHandle::num_states_);
    std::fill(visits.begin(), visits.end(), 0);
    visits[TrxHandle::S_REPLICATING] = 1;
    visits[TrxHandle::S_CERTIFYING] = 1;
    visits[TrxHandle::S_APPLYING] = 1;
    visits[TrxHandle::S_COMMITTING] = 1;
    visits[TrxHandle::S_COMMITTED] = 1;

    check_states_graph(state_trans_slave, ts.get(), visits);
}
END_TEST

START_TEST(test_serialization)
{
    TrxHandleMaster::Pool lp(4096, 16, "serialization_lp");
    TrxHandleSlave::Pool  sp(sizeof(TrxHandleSlave), 16, "serialization_sp");

    for (int version = 3; version <= 5; ++version)
    {
        galera::TrxHandleMaster::Params const trx_params("", version,
                                                         KeySet::MAX_VERSION);
        wsrep_uuid_t uuid;
        gu_uuid_generate(&uuid, 0, 0);
        TrxHandleMasterPtr trx
            (TrxHandleMaster::New(lp, trx_params, uuid, 4567, 8910),
             TrxHandleMasterDeleter());

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);
        ck_assert(buf.size() > 0);

        TrxHandleSlavePtr txs1(TrxHandleSlave::New(false, sp),
                               TrxHandleSlaveDeleter());
        gcs_action const act =
            { 1, 2, buf.data(), int(buf.size()), GCS_ACT_WRITESET};
        ck_assert(txs1->unserialize<true>(act) > 0);
        ck_assert(txs1->global_seqno() == act.seqno_g);
        ck_assert(txs1->local_seqno()  == act.seqno_l);
    }
}
END_TEST

static enum wsrep_cb_status
apply_cb(
    void*                   ctx,
    const wsrep_ws_handle_t* wh,
    uint32_t                flags,
    const wsrep_buf_t*      data,
    const wsrep_trx_meta_t* meta,
    wsrep_bool_t*           exit_loop
    )
{
    std::vector<char>* const res(static_cast<std::vector<char>* >(ctx));
    ck_assert(NULL != res);

    const char* const c(static_cast<const char*>(data->ptr));
    ck_assert(NULL != c);
    ck_assert(1 == data->len);

    res->push_back(*c);

    return WSREP_CB_SUCCESS;
}

START_TEST(test_streaming)
{
    TrxHandleMaster::Pool lp(4096, 16, "streaming_lp");
    TrxHandleSlave::Pool  sp(sizeof(TrxHandleSlave), 16, "streaming_sp");

    int const version(galera::WriteSetNG::VER5);
    galera::TrxHandleMaster::Params const trx_params("", version,
                                                     KeySet::MAX_VERSION);
    wsrep_uuid_t uuid;
    gu_uuid_generate(&uuid, 0, 0);
    TrxHandleMasterPtr trx(TrxHandleMaster::New(lp, trx_params, uuid, 4567,8910),
                           TrxHandleMasterDeleter());

    galera::TrxHandleLock lock(*trx);

    std::vector<char> src(3); // initial wirteset
    src[0] = 'a'; src[1] = 'b'; src[2] = 'c';

    std::vector<char> res;          // apply_cb should reproduce src in res
    ck_assert(src != res);

    ck_assert(trx->flags() & TrxHandle::F_BEGIN);

    {
        // 0. first fragment A
        trx->append_data(&src[0], 1, WSREP_DATA_ORDERED, false);
        trx->finalize(0);

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);

        ck_assert(buf.size() > 0);
        trx->release_write_set_out();

        TrxHandleSlavePtr ts(TrxHandleSlave::New(false, sp),
                             TrxHandleSlaveDeleter());
        gcs_action const act =
            { 1, 2, buf.data(), int(buf.size()), GCS_ACT_WRITESET};
        ck_assert(ts->unserialize<true>(act) > 0);
        ck_assert(ts->flags() & TrxHandle::F_BEGIN);
        ck_assert(!(ts->flags() & TrxHandle::F_COMMIT));
        trx->add_replicated(ts);

        wsrep_bool_t exit_loop;
        ts->apply(&res, apply_cb, wsrep_trx_meta_t(), exit_loop);
    }

    {
        // 1. middle fragment B
        trx->append_data(&src[1], 1, WSREP_DATA_ORDERED, false);
        trx->finalize(1);

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);
        ck_assert(buf.size() > 0);
        trx->release_write_set_out();

        TrxHandleSlavePtr ts(TrxHandleSlave::New(false, sp),
                             TrxHandleSlaveDeleter());
        gcs_action const act =
            { 2, 3, buf.data(), int(buf.size()), GCS_ACT_WRITESET};
        ck_assert(ts->unserialize<true>(act) > 0);
        ck_assert(!(ts->flags() & TrxHandle::F_BEGIN));
        ck_assert(!(ts->flags() & TrxHandle::F_COMMIT));
        trx->add_replicated(ts);

        wsrep_bool_t exit_loop;
        ts->apply(&res, apply_cb, wsrep_trx_meta_t(), exit_loop);
    }

    {
        // 2. last fragment C
        trx->append_data(&src[2], 1, WSREP_DATA_ORDERED, false);
        trx->set_flags(TrxHandle::F_COMMIT); // commit
        trx->finalize(2);

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);
        ck_assert(buf.size() > 0);
        trx->release_write_set_out();

        TrxHandleSlavePtr ts(TrxHandleSlave::New(false, sp),
                             TrxHandleSlaveDeleter());
        gcs_action const act =
            { 3, 4, buf.data(), int(buf.size()), GCS_ACT_WRITESET};
        ck_assert(ts->unserialize<true>(act) > 0);
        ck_assert(!(ts->flags() & TrxHandle::F_BEGIN));
        ck_assert(ts->flags() & TrxHandle::F_COMMIT);
        trx->add_replicated(ts);

        wsrep_bool_t exit_loop;
        ts->apply(&res, apply_cb, wsrep_trx_meta_t(), exit_loop);
    }

    ck_assert(res == src);
}
END_TEST

Suite* trx_handle_suite()
{
    Suite* s = suite_create("trx_handle");
    TCase* tc;

    tc = tcase_create("test_states_master");
    tcase_add_test(tc, test_states_master);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_states_slave");
    tcase_add_test(tc, test_states_slave);
    suite_add_tcase(s, tc);


    tc = tcase_create("test_serialization");
    tcase_add_test(tc, test_serialization);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_streaming");
    tcase_add_test(tc, test_streaming);
    suite_add_tcase(s, tc);

    return s;
}
