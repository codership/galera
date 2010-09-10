//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"

#include <check.h>

using namespace std;
using namespace galera;

START_TEST(test_states)
{
    wsrep_uuid_t uuid = {{1, }};

    // first check basic stuff
    // 1) initial state is executing
    // 2) invalid state changes are caught
    // 3) valid state changes change state
    TrxHandle* trx(new TrxHandle(uuid, -1, 1, true));

    log_info << *trx;
    fail_unless(trx->state() == TrxHandle::S_EXECUTING);
    try
    {
        trx->set_state(TrxHandle::S_COMMITTED);
        fail("");
    }
    catch (...)
    {
        fail_unless(trx->state() == TrxHandle::S_EXECUTING);
    }
    trx->set_state(TrxHandle::S_REPLICATING);
    fail_unless(trx->state() == TrxHandle::S_REPLICATING);
    trx->unref();

    // abort before replication
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unref();

    // aborted during replication and does not certify
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unref();

    // aborted during replication and certifies but does not certify
    // during replay (is this even possible?)
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unref();

    // aborted during replication, certifies and commits
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_CERTIFIED);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_REPLAYED);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();

    // aborted during certification, replays and commits
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_REPLICATED);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_CERTIFIED);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_REPLAYED);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();

    // aborted while waiting applying, replays and commits
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_REPLICATED);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_CERTIFIED);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_REPLAYED);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();

    // smooth operation
    trx = new TrxHandle(uuid, -1, 1, true);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_REPLICATED);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_CERTIFIED);
    trx->set_state(TrxHandle::S_APPLYING);
    trx->set_state(TrxHandle::S_COMMITTED);
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

    return s;
}
