//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "galera_exception.hpp"

#include <gu_serialize.hpp>
#include <gu_uuid.hpp>

const galera::TrxHandleMaster::Params
galera::TrxHandleMaster::Defaults(".", -1, KeySet::MAX_VERSION,
                                  gu::RecordSet::VER2, false);

void galera::TrxHandle::print_state(std::ostream& os, TrxHandle::State s)
{
    switch (s)
    {
    case TrxHandle::S_EXECUTING:
        os << "EXECUTING"; return;
    case TrxHandle::S_MUST_ABORT:
        os << "MUST_ABORT"; return;
    case TrxHandle::S_ABORTING:
        os << "ABORTING"; return;
    case TrxHandle::S_REPLICATING:
        os << "REPLICATING"; return;
    case TrxHandle::S_CERTIFYING:
        os << "CERTIFYING"; return;
    case TrxHandle::S_MUST_REPLAY:
        os << "MUST_REPLAY"; return;
    case TrxHandle::S_REPLAYING:
        os << "REPLAYING"; return;
    case TrxHandle::S_APPLYING:
        os << "APPLYING"; return;
    case TrxHandle::S_COMMITTING:
        os << "COMMITTING"; return;
    case TrxHandle::S_ROLLING_BACK:
        os << "ROLLING_BACK"; return;
    case TrxHandle::S_COMMITTED:
        os << "COMMITTED"; return;
    case TrxHandle::S_ROLLED_BACK:
        os << "ROLLED_BACK"; return;
    // don't use default to make compiler warn if something is missed
    }

    os << "<unknown TRX state " << s << ">";
    assert(0);
}

std::ostream& galera::operator<<(std::ostream& os, TrxHandle::State const s)
{
    galera::TrxHandle::print_state(os, s);
    return os;
}

void galera::TrxHandle::print_set_state(State state) const
{
    log_info << "Trx: " << this << " shifting to " << state;
}

void galera::TrxHandle::print_state_history(std::ostream& os) const
{
    const std::vector<TrxHandle::Fsm::StateEntry>& hist(state_.history());
    for (size_t i(0); i < hist.size(); ++i)
    {
        os << hist[i].first << ':' << hist[i].second << "->";
    }

    const TrxHandle::Fsm::StateEntry current_state(state_.get_state_entry());
    os << current_state.first << ':' << current_state.second;
}

inline
void galera::TrxHandle::print(std::ostream& os) const
{
    os << "source: "   << source_id()
       << " version: " << version()
       << " local: "   << local()
       << " flags: "   << flags()
       << " conn_id: " << int64_t(conn_id())
       << " trx_id: "  << int64_t(trx_id())  // for readability
       << " tstamp: "  << timestamp()
       << "; state: ";
    print_state_history(os);
}

std::ostream&
galera::operator<<(std::ostream& os, const TrxHandle& th)
{
    th.print(os); return os;
}

void
galera::TrxHandleSlave::print(std::ostream& os) const
{
    TrxHandle::print(os);

    os << " seqnos (l: " << local_seqno_
       << ", g: "        << global_seqno_
       << ", s: "        << last_seen_seqno_
       << ", d: "        << depends_seqno_
       << ")";

    if (!skip_event())
    {
        os << " WS pa_range: " << write_set().pa_range();

        if (write_set().annotated())
        {
            os << "\nAnnotation:\n";
            write_set().write_annotation(os);
            os << std::endl;
        }
    }
    else
    {
        os << " skip event";
    }

    os << "; state history: ";
    print_state_history(os);
}

std::ostream&
galera::operator<<(std::ostream& os, const TrxHandleSlave& th)
{
    th.print(os); return os;
}


galera::TrxHandleMaster::Fsm::TransMap galera::TrxHandleMaster::trans_map_;
galera::TrxHandleSlave::Fsm::TransMap galera::TrxHandleSlave::trans_map_;


namespace galera {

//
// About transaction states:
//
// The TrxHandleMaster stats are used to track the state of the
// transaction, while TrxHandleSlave states are used to track
// which critical sections have been accessed during write set
// applying. As a convention, TrxHandleMaster states are changed
// before entering the critical section, TrxHandleSlave states
// after critical section has been succesfully entered.
//
// TrxHandleMaster states during normal execution:
//
// EXECUTING   - Transaction handle has been created by appending key
//               or write set data
// REPLICATING - Transaction write set has been send to group
//               communication layer for ordering
// CERTIFYING  - Transaction write set has been received from group
//               communication layer, has entered local monitor and
//               is certifying
// APPLYING    - Transaction has entered applying critical section
// COMMITTING  - Transaction has entered committing critical section
// COMMITTED   - Transaction has released commit time critical section
// ROLLED_BACK - Application performed a voluntary rollback
//
// Note that streaming replication rollback happens by replicating
// special rollback writeset which will go through regular write set
// critical sections.
//
// Note/Fixme: CERTIFYING, APPLYING and COMMITTING states seem to be
//             redundant as these states can be tracked via
//             associated TrxHandleSlave states.
//
//
// TrxHandleMaster states after effective BF abort:
//
// MUST_ABORT   - Transaction enter this state after succesful BF abort.
//                BF abort is allowed if:
//                * Transaction does not have associated TrxHandleSlave
//                * Transaction has associated TrxHandleSlave but it does
//                  not have commit flag set
//                * Transaction has associated TrxHandleSlave, commit flag
//                  is set and the TrxHandleSlave global sequence number is
//                  higher than BF aborter global sequence number
//
// 1) If the certification after BF abort results a failure:
// ABORTING     - BF abort was effective and certification
//                resulted a failure
// ROLLING_BACK - Commit order critical section has been grabbed for
//                rollback
// ROLLED_BACK  - Commit order critical section has been released after
//                succesful rollback
//
// 2) The case where BF abort happens after succesful certification or
//    if out-of-order certification results a success:
// MUST_REPLAY  - The transaction must roll back and replay in applier
//                context.
//                * If the BF abort happened before certification,
//                  certification must be performed in applier context
//                  and the transaction replay must be aborted if
//                  the certification fails.
//                * TrxHandleSlave state can be used to determine
//                  which critical sections must be entered before the
//                  replay. For example, if the TrxHandleSlave state is
//                  REPLICATING, write set must be certified under local
//                  monitor and both apply and commit monitors must be
//                  entered before applying. On the other hand, if
//                  TrxHandleSlave state is APPLYING, only commit monitor
//                  must be grabbed before replay.
//
// TrxHandleMaster states after replication failure:
//
// ABORTING     - Replicaition resulted a failure
// ROLLING_BACK - Error has been returned to application
// ROLLED_BACK  - Application has finished rollback
//
//
// TrxHandleMaster states after certification failure:
//
// ABORTING - Certification resulted a failure
// ROLLING_BACK - Commit order critical section has been grabbed for
//                rollback
// ROLLED_BACK  - Commit order critical section has been released
//                after succesful rollback
//
//
//
// TrxHandleSlave:
// REPLICATING - this is the first state for TrxHandleSlave after it
//               has been received from group
// CERTIFYING  - local monitor has been entered succesfully
// APPLYING    - apply monitor has been entered succesfully
// COMMITTING  - commit monitor has been entered succesfully
// ABORTING    - certification has failed
// ROLLING_BACK - certification has failed and commit monitor has been
//                entered
// COMMITTED   - commit has been finished, commit order critical section
//               has been released
// ROLLED_BACK - transaction has rolled back, commit order critical section
//               has been released
//
//
// State machine diagrams can be found below.

template<>
TransMapBuilder<TrxHandleMaster>::TransMapBuilder()
    :
    trans_map_(TrxHandleMaster::trans_map_)
{
    //
    //  0                                                   COMMITTED <-|
    //  |                                                         ^     |
    //  |                             SR                          |     |
    //  |  |------------------------------------------------------|     |
    //  v  v                                                      |     |
    // EXECUTING -> REPLICATING -> CERTIFYING -> APPLYING -> COMMITTING |
    //  |^ |            |               |            |            |     |
    //  || |-------------------------------------------------------     |
    //  || | BF Abort   ----------------|                               |
    //  || v            |   Cert Fail                                   |
    //  ||MUST_ABORT -----------------------------------------          |
    //  ||              |           |                         |         |
    //  ||     Pre Repl |           v                         |    REPLAYING
    //  ||              |  MUST_CERT_AND_REPLAY --------------|          ^
    //  || SR Rollback  v           |               ----------| Cert OK  |
    //  | --------- ABORTING <-------               |         v          |
    //  |               |        Cert Fail          |   MUST_REPLAY_AM   |
    //  |               v                           |         |          |
    //  |          ROLLING_BACK                     |         v          |
    //  |               |                           |-> MUST_REPLAY_CM   |
    //  |               v                           |         |          |
    //  ----------> ROLLED_BACK                     |         v          |
    //                                              |-> MUST_REPLAY      |
    //                                                        |          |
    //                                                        ------------
    //

    // Executing
    add(TrxHandle::S_EXECUTING,  TrxHandle::S_REPLICATING);
    add(TrxHandle::S_EXECUTING,  TrxHandle::S_ROLLED_BACK);
    add(TrxHandle::S_EXECUTING,  TrxHandle::S_MUST_ABORT);

    // Replicating
    add(TrxHandle::S_REPLICATING, TrxHandle::S_CERTIFYING);
    add(TrxHandle::S_REPLICATING, TrxHandle::S_MUST_ABORT);

    // Certifying
    add(TrxHandle::S_CERTIFYING, TrxHandle::S_APPLYING);
    add(TrxHandle::S_CERTIFYING, TrxHandle::S_ABORTING);
    add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_ABORT);

    // Applying
    add(TrxHandle::S_APPLYING, TrxHandle::S_COMMITTING);
    add(TrxHandle::S_APPLYING, TrxHandle::S_MUST_ABORT);

    // Committing
    add(TrxHandle::S_COMMITTING, TrxHandle::S_COMMITTED);
    add(TrxHandle::S_COMMITTING, TrxHandle::S_MUST_ABORT);
    add(TrxHandle::S_COMMITTED, TrxHandle::S_EXECUTING); // SR

    // BF aborted
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY);
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_ABORTING);

    // Replay, BF abort happens on application side after
    // commit monitor has been grabbed
    add(TrxHandle::S_MUST_REPLAY, TrxHandle::S_REPLAYING);
    // In-order certification failed for BF'ed action
    add(TrxHandle::S_MUST_REPLAY, TrxHandle::S_ABORTING);

    // Replay stage
    add(TrxHandle::S_REPLAYING, TrxHandle::S_COMMITTING);

    // BF aborted
    add(TrxHandle::S_ABORTING,     TrxHandle::S_ROLLED_BACK);

    // cert failed or BF in apply monitor
    add(TrxHandle::S_ABORTING,  TrxHandle::S_ROLLING_BACK);
    add(TrxHandle::S_ROLLING_BACK, TrxHandle::S_ROLLED_BACK);

    // SR rollback
    add(TrxHandle::S_ABORTING, TrxHandle::S_EXECUTING);
}


template<>
TransMapBuilder<TrxHandleSlave>::TransMapBuilder()
    :
    trans_map_(TrxHandleSlave::trans_map_)
{
    //                                 Cert OK
    // 0 --> REPLICATING -> CERTIFYING ------> APPLYING --> COMMITTING
    //            |             |                               |
    //            |             |Cert failed                    |
    //            |             |                               |
    //            |             v                               v
    //            +-------> ABORTING                  COMMITTED / ROLLED_BACK
    //                          |
    //                          v
    //                    ROLLING_BACK
    //                          |
    //                          v
    //                     ROLLED_BACK

    // Enter in-order cert after replication
    add(TrxHandle::S_REPLICATING, TrxHandle::S_CERTIFYING);
    // BF'ed and IST-skipped
    add(TrxHandle::S_REPLICATING, TrxHandle::S_ABORTING);
    // Applying after certification
    add(TrxHandle::S_CERTIFYING,  TrxHandle::S_APPLYING);
    // Roll back due to cert failure
    add(TrxHandle::S_CERTIFYING,  TrxHandle::S_ABORTING);
    // Entering commit monitor after rollback
    add(TrxHandle::S_ABORTING,    TrxHandle::S_ROLLING_BACK);
    // Entering commit monitor after applying
    add(TrxHandle::S_APPLYING,    TrxHandle::S_COMMITTING);
    // Replay after BF
    add(TrxHandle::S_APPLYING,    TrxHandle::S_REPLAYING);
    add(TrxHandle::S_COMMITTING,  TrxHandle::S_REPLAYING);
    // Commit finished
    add(TrxHandle::S_COMMITTING,  TrxHandle::S_COMMITTED);
    // Error reported in leave_commit_order() call
    add(TrxHandle::S_COMMITTING,  TrxHandle::S_ROLLED_BACK);
    // Rollback finished
    add(TrxHandle::S_ROLLING_BACK,  TrxHandle::S_ROLLED_BACK);
}


static TransMapBuilder<TrxHandleMaster> master;
static TransMapBuilder<TrxHandleSlave> slave;

} /* namespace galera */

void
galera::TrxHandleSlave::sanity_checks() const
{
    if (gu_unlikely((flags() & (F_ROLLBACK | F_BEGIN)) ==
                    (F_ROLLBACK | F_BEGIN)))
    {
        log_warn << "Both F_BEGIN and F_ROLLBACK are set on trx. "
                 << "This trx should not have been replicated at all: "
                 << *this;
        assert(0);
    }
}

void
galera::TrxHandleSlave::deserialize_error_log(const gu::Exception& e) const
{
    log_fatal << "Writeset deserialization failed: " << e.what()
              << std::endl << "WS flags:      " << write_set_flags_
              << std::endl << "Trx proto:     " << version_
              << std::endl << "Trx source:    " << source_id_
              << std::endl << "Trx conn_id:   " << conn_id_
              << std::endl << "Trx trx_id:    " << trx_id_
              << std::endl << "Trx last_seen: " << last_seen_seqno_;
}

void
galera::TrxHandleSlave::apply (void*                   recv_ctx,
                               wsrep_apply_cb_t        apply_cb,
                               const wsrep_trx_meta_t& meta,
                               wsrep_bool_t&           exit_loop)
{
    uint32_t const wsrep_flags(trx_flags_to_wsrep_flags(flags()));

    int err(0);

    const DataSetIn& ws(write_set_.dataset());
    void*  err_msg(NULL);
    size_t err_len(0);

    ws.rewind(); // make sure we always start from the beginning

    wsrep_ws_handle_t const wh = { trx_id(), this };

    if (ws.count() > 0)
    {
        for (ssize_t i = 0; WSREP_CB_SUCCESS == err && i < ws.count(); ++i)
        {
            const gu::Buf& buf(ws.next());
            wsrep_buf_t const wb = { buf.ptr, size_t(buf.size) };
            err = apply_cb(recv_ctx, &wh, wsrep_flags, &wb, &meta, &exit_loop);
        }
    }
    else
    {
        // Apply also zero sized write set to inform application side
        // about transaction meta data.
        wsrep_buf_t const wb = { NULL, 0 };
        err = apply_cb(recv_ctx, &wh, wsrep_flags, &wb, &meta, &exit_loop);
        assert(NULL == err_msg);
        assert(0    == err_len);
    }

    if (gu_unlikely(err != WSREP_CB_SUCCESS))
    {
        std::ostringstream os;

        os << "Apply callback failed: Trx: " << *this
           << ", status: " << err;

        galera::ApplyException ae(os.str(), err_msg, NULL, err_len);

        GU_TRACE(ae);

        throw ae;
    }

    return;
}


/* we don't care about any failures in applying unordered events */
void
galera::TrxHandleSlave::unordered(void*                recv_ctx,
                                  wsrep_unordered_cb_t cb) const
{
    if (NULL != cb && write_set_.unrdset().count() > 0)
    {
        const DataSetIn& unrd(write_set_.unrdset());
        for (int i(0); i < unrd.count(); ++i)
        {
            const gu::Buf& data(unrd.next());
            wsrep_buf_t const wb = { data.ptr, size_t(data.size) };
            cb(recv_ctx, &wb);
        }
    }
}


void
galera::TrxHandleSlave::destroy_local(void*  ptr)
{
    assert(ptr);
    (static_cast<TrxHandleMaster*>(ptr))->~TrxHandleMaster();
}

