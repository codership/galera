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
    TrxHandleMaster* trx(TrxHandleMaster::New(tp, TrxHandleMaster::Defaults, uuid, -1, 1));
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
    galera::TrxHandleMaster::Params const trx_params("", version,KeySet::MAX_VERSION);
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

    return s;
}
