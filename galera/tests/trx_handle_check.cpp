//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include <gu_uuid.hpp>

#include <check.h>

using namespace std;
using namespace galera;

START_TEST(test_states)
{
    TrxHandle::LocalPool tp(TrxHandle::LOCAL_STORAGE_SIZE, 16, "test_states");
    wsrep_uuid_t uuid = {{1, }};

    // first check basic stuff
    // 1) initial state is executing
    // 2) invalid state changes are caught
    // 3) valid state changes change state
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());

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
        fail_unless(trx->state() == TrxHandle::S_REPLICATING);
    }

    // abort before replication
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_ABORTING);
        trx->set_state(TrxHandle::S_ROLLED_BACK);
    }

    // aborted during replication and does not certify
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_ABORTING);
        trx->set_state(TrxHandle::S_ROLLED_BACK);
    }

    // aborted during replication and certifies but does not certify
    // during replay (is this even possible?)
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_ABORTING);
        trx->set_state(TrxHandle::S_ROLLED_BACK);
    }

    // aborted during replication, certifies and commits
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
        trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
        trx->set_state(TrxHandle::S_MUST_REPLAY);
        trx->set_state(TrxHandle::S_REPLAYING);
        trx->set_state(TrxHandle::S_COMMITTED);
    }

    // aborted during certification, replays and commits
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
        trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
        trx->set_state(TrxHandle::S_MUST_REPLAY);
        trx->set_state(TrxHandle::S_REPLAYING);
        trx->set_state(TrxHandle::S_COMMITTED);
    }

    // aborted while waiting applying, replays and commits
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_APPLYING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
        trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
        trx->set_state(TrxHandle::S_MUST_REPLAY);
        trx->set_state(TrxHandle::S_REPLAYING);
        trx->set_state(TrxHandle::S_COMMITTED);
    }

    // aborted while waiting for commit order, replays and commits
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_APPLYING);
        trx->set_state(TrxHandle::S_COMMITTING);
        trx->set_state(TrxHandle::S_MUST_ABORT);
        trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
        trx->set_state(TrxHandle::S_MUST_REPLAY);
        trx->set_state(TrxHandle::S_REPLAYING);
        trx->set_state(TrxHandle::S_COMMITTED);
    }

    // smooth operation
    {
        TrxHandlePtr trx(TrxHandle::New(tp, TrxHandle::Defaults, uuid, -1, 1),
                         TrxHandleDeleter());
        trx->set_state(TrxHandle::S_REPLICATING);
        trx->set_state(TrxHandle::S_CERTIFYING);
        trx->set_state(TrxHandle::S_APPLYING);
        trx->set_state(TrxHandle::S_COMMITTING);
        trx->set_state(TrxHandle::S_COMMITTED);
    }
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
