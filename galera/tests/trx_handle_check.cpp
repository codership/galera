//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "uuid.hpp"

#include <check.h>

using namespace std;
using namespace galera;

START_TEST(test_states)
{
    TrxHandleMaster::Pool tp(TrxHandleMaster::LOCAL_STORAGE_SIZE, 16,
                             "test_states_master");
    TrxHandleSlave::Pool  sp(sizeof(TrxHandleSlave), 16, "test_states_slave");

    wsrep_uuid_t uuid = {{1, }};

    // first check basic stuff
    // 1) initial state is executing
    // 2) invalid state changes are caught
    // 3) valid state changes change state
    TrxHandleMaster* trx(TrxHandleMaster::New(tp, TrxHandleMaster::Defaults,
                                              uuid, -1, 1));
    trx->lock();

    log_info << *trx;
    fail_unless(trx->state() == TrxHandle::S_EXECUTING);

#if 0 // now setting wrong state results in abort
    try
    {
        trx->set_state(TrxHandle::S_COMMITTED);
        fail("");
    }
    catch (...)
    {
        fail_unless(trx->state() == TrxHandle::S_EXECUTING);
    }
#endif

    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_COMMITTING);
    fail_unless(trx->state() == TrxHandle::S_COMMITTING);
    trx->unlock();
    trx->unref();

    // abort before replication
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unlock();
    trx->unref();

    // aborted during replication and does not certify
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unlock();
    trx->unref();

    // aborted during replication and certifies but does not certify
    // during replay (is this even possible?)
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
//    trx->set_state(TrxHandle::S_EXECUTING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unlock();
    trx->unref();

    // aborted during replication, certifies and commits
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
//    trx->set_state(TrxHandle::S_EXECUTING);
    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unlock();
    trx->unref();

    // aborted during certification, replays and commits
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
//    trx->set_state(TrxHandle::S_EXECUTING);
    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unlock();
    trx->unref();

    // aborted while waiting applying, replays and commits
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unlock();
    trx->unref();

    // aborted while waiting for commit order, replays and commits
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_COMMITTING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unlock();
    trx->unref();

    // smooth operation master
    trx = TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1);
    trx->lock();
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_COMMITTING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unlock();
    trx->unref();

    // smooth operation slave
    TrxHandleSlave* txs(TrxHandleSlave::New(sp));
    txs->lock();
    txs->set_state(TrxHandle::S_CERTIFYING);
    txs->set_state(TrxHandle::S_APPLYING);
    txs->set_state(TrxHandle::S_COMMITTING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->unlock();
    txs->unref();

    // certification failure slave
    txs = TrxHandleSlave::New(sp);
    txs->lock();
    txs->set_state(TrxHandle::S_CERTIFYING);
    txs->set_state(TrxHandle::S_MUST_ABORT);
    txs->set_state(TrxHandle::S_ROLLED_BACK);
    txs->unlock();
    txs->unref();

    // replaying fragment aborted BEFORE certification
    txs = TrxHandleSlave::New(sp);
    txs->lock();
    txs->set_state(TrxHandle::S_MUST_ABORT);
    txs->set_state(TrxHandle::S_REPLICATING);
    txs->set_state(TrxHandle::S_REPLAYING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->unlock();
    txs->unref();

    // replaying fragment aborted AFTER certification
    txs = TrxHandleSlave::New(sp);
    txs->lock();
    txs->set_state(TrxHandle::S_MUST_ABORT);
    txs->set_state(TrxHandle::S_MUST_REPLAY);
    txs->set_state(TrxHandle::S_REPLAYING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->unlock();
    txs->unref();

    // replaying replicating fragment
    txs = TrxHandleSlave::New(sp);
    txs->lock();
    txs->set_state(TrxHandle::S_REPLAYING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->unlock();
    txs->unref();

    // replaying certifying fragment
    txs = TrxHandleSlave::New(sp);
    txs->lock();
    txs->set_state(TrxHandle::S_CERTIFYING);
    txs->set_state(TrxHandle::S_REPLAYING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->unlock();
    txs->unref();

    // replaying committed fragment
    txs = TrxHandleSlave::New(sp);
    txs->lock();
    txs->set_state(TrxHandle::S_CERTIFYING);
    txs->set_state(TrxHandle::S_APPLYING);
    txs->set_state(TrxHandle::S_COMMITTING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->set_state(TrxHandle::S_REPLAYING);
    txs->set_state(TrxHandle::S_COMMITTED);
    txs->unlock();
    txs->unref();
}
END_TEST

START_TEST(test_serialization)
{
    TrxHandleMaster::Pool lp(4096, 16, "serialization_lp");
    TrxHandleSlave::Pool  sp(sizeof(TrxHandleSlave), 16, "serialization_sp");

    int const version(3);
    galera::TrxHandleMaster::Params const trx_params("", version,
                                                     KeySet::MAX_VERSION);
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);
    TrxHandleMaster* trx(TrxHandleMaster::New(lp, trx_params, uuid, 4567, 8910));

    std::vector<gu::byte_t> buf;
    trx->serialize(0, buf);
    fail_unless(buf.size() > 0);

    TrxHandleSlave* txs1(TrxHandleSlave::New(sp));
    fail_unless(txs1->unserialize(&buf[0], buf.size(), 0) > 0);
    txs1->unref();

    trx->unref();
}
END_TEST

static enum wsrep_cb_status
apply_cb(
    void*                   ctx,
    const void*             data,
    size_t                  size,
    uint32_t                flags,
    const wsrep_trx_meta_t* meta
    )
{
    std::vector<char>* const res(static_cast<std::vector<char>* >(ctx));
    fail_if(NULL == res);

    const char* const c(static_cast<const char*>(data));
    fail_if(NULL == c);
    fail_if(1 != size);

    res->push_back(*c);

    return WSREP_CB_SUCCESS;
}

START_TEST(test_streaming)
{
    TrxHandleMaster::Pool lp(4096, 16, "streaming_lp");
    TrxHandleSlave::Pool  sp(sizeof(TrxHandleSlave), 16, "streaming_sp");

    int const version(3);
    galera::TrxHandleMaster::Params const trx_params("", version,
                                                     KeySet::MAX_VERSION);
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);
    TrxHandleMaster* trx(TrxHandleMaster::New(lp, trx_params, uuid, 4567, 8910));
    trx->lock();

    std::vector<char> src(3); // initial wirteset
    src[0] = 'a'; src[1] = 'b'; src[2] = 'c';

    std::vector<char> res;          // apply_cb should reproduce src in res
    fail_if(src == res);
    {
        // 0. first fragment A
        trx->append_data(&src[0], 1, WSREP_DATA_ORDERED, false);
        trx->set_flags(0); // no special flags

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);
        fail_unless(buf.size() > 0);
        trx->release_write_set_out();

        TrxHandleSlave* txs(trx->repld());
        fail_unless(txs->unserialize(&buf[0], buf.size(), 0) > 0);
        fail_unless(txs->flags() & TrxHandle::F_BEGIN);
        fail_if(txs->flags() & TrxHandle::F_COMMIT);
        txs->apply(&res, apply_cb, wsrep_trx_meta_t());
    }
    trx->add_replicated(TrxHandleSlave::New(sp));
    {
        // 1. middle fragment B
        trx->append_data(&src[1], 1, WSREP_DATA_ORDERED, false);
        trx->set_flags(0); // no special flags

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);
        fail_unless(buf.size() > 0);
        trx->release_write_set_out();

        TrxHandleSlave* txs(trx->repld());
        fail_unless(txs->unserialize(&buf[0], buf.size(), 0) > 0);
        fail_if(txs->flags() & TrxHandle::F_BEGIN);
        fail_if(txs->flags() & TrxHandle::F_COMMIT);
        txs->apply(&res, apply_cb, wsrep_trx_meta_t());
    }
    trx->add_replicated(TrxHandleSlave::New(sp));
    {
        // 2. last fragment C
        trx->append_data(&src[2], 1, WSREP_DATA_ORDERED, false);
        trx->set_flags(TrxHandle::F_COMMIT); // commit

        std::vector<gu::byte_t> buf;
        trx->serialize(0, buf);
        fail_unless(buf.size() > 0);
        trx->release_write_set_out();

        TrxHandleSlave* txs(trx->repld());
        fail_unless(txs->unserialize(&buf[0], buf.size(), 0) > 0);
        fail_if(txs->flags() & TrxHandle::F_BEGIN);
        fail_unless(txs->flags() & TrxHandle::F_COMMIT);
        txs->apply(&res, apply_cb, wsrep_trx_meta_t());
    }

    trx->unlock();
    trx->unref(); // after this point replicated trx handles should be unrefd too

    fail_if(res != src);
}
END_TEST

Suite* trx_handle_suite()
{
    Suite* s = suite_create("trx_handle");
    TCase* tc;

    tc = tcase_create("test_states");
    tcase_add_test(tc, test_states);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_serialization");
    tcase_add_test(tc, test_serialization);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_streaming");
    tcase_add_test(tc, test_streaming);
    suite_add_tcase(s, tc);

    return s;
}
