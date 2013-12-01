//
// Copyright (C) 2010-2013 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "uuid.hpp"

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
    TrxHandle* trx(new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0));

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
    trx->unref();

    // abort before replication
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unref();

    // aborted during replication and does not certify
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unref();

    // aborted during replication and certifies but does not certify
    // during replay (is this even possible?)
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    trx->set_state(TrxHandle::S_ROLLED_BACK);
    trx->unref();

    // aborted during replication, certifies and commits
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();

    // aborted during certification, replays and commits
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();

    // aborted while waiting applying, replays and commits
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_APPLYING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();

    // aborted while waiting for commit order, replays and commits
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_APPLYING);
    trx->set_state(TrxHandle::S_COMMITTING);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
    trx->set_state(TrxHandle::S_MUST_REPLAY);
    trx->set_state(TrxHandle::S_REPLAYING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();


    // smooth operation
    trx = new TrxHandle(TrxHandle::Defaults, uuid, -1, 1, NULL, 0);
    trx->set_state(TrxHandle::S_REPLICATING);
    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_APPLYING);
    trx->set_state(TrxHandle::S_COMMITTING);
    trx->set_state(TrxHandle::S_COMMITTED);
    trx->unref();


}
END_TEST


START_TEST(test_serialization)
{
    int const version(0);
    galera::TrxHandle::Params const trx_params("", version,KeySet::MAX_VERSION);
    wsrep_uuid_t uuid;
    gu_uuid_generate(reinterpret_cast<gu_uuid_t*>(&uuid), 0, 0);
    TrxHandle* trx
        ((new TrxHandleWithStore(trx_params, uuid, 4567, 8910))->handle());

    fail_unless(trx->serial_size() == 4 + 16 + 8 + 8 + 8 + 8);

    trx->set_flags(trx->flags() | TrxHandle::F_MAC_HEADER);
    fail_unless(trx->serial_size() == 4 + 16 + 8 + 8 + 8 + 8 + 2);
    trx->set_flags(trx->flags() & ~TrxHandle::F_MAC_HEADER);
    fail_unless(trx->serial_size() == 4 + 16 + 8 + 8 + 8 + 8);

    trx->append_annotation(reinterpret_cast<const gu::byte_t*>("foobar"),
                           strlen("foobar"));
    trx->set_flags(trx->flags() | TrxHandle::F_ANNOTATION);
    fail_unless(trx->serial_size() == 4 + 16 + 8 + 8 + 8 + 8 + 4 + 6);
    trx->set_flags(trx->flags() & ~TrxHandle::F_ANNOTATION);
    fail_unless(trx->serial_size() == 4 + 16 + 8 + 8 + 8 + 8);

    trx->set_last_seen_seqno(0);

    TrxHandle* trx2(new TrxHandle());

    std::vector<gu::byte_t> buf(trx->serial_size());
    fail_unless(trx->serialize(&buf[0], buf.size(), 0) > 0);
    fail_unless(trx2->unserialize(&buf[0], buf.size(), 0) > 0);

    trx->set_flags(trx->flags() | TrxHandle::F_MAC_PAYLOAD);
    buf.resize(trx->serial_size());
    fail_unless(trx->serialize(&buf[0], buf.size(), 0) > 0);
    fail_unless(trx2->unserialize(&buf[0], buf.size(), 0) > 0);

    trx->set_flags(trx->flags() | TrxHandle::F_ANNOTATION);
    buf.resize(trx->serial_size());
    fail_unless(trx->serialize(&buf[0], buf.size(), 0) > 0);
    fail_unless(trx2->unserialize(&buf[0], buf.size(), 0) > 0);
    fail_unless(trx2->serial_size() == trx->serial_size(),
                "got serial_size(*trx2) = %zu, serial_size(*trx) = %zu",
                trx2->serial_size(), trx->serial_size());

    trx2->unref();
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
