//
// Copyright (C) 2010-2016 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "galera_exception.hpp"

#include <gu_serialize.hpp>
#include <gu_uuid.hpp>

const galera::TrxHandleMaster::Params
galera::TrxHandleMaster::Defaults(".", -1, KeySet::MAX_VERSION, false);

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
    case TrxHandle::S_MUST_CERT_AND_REPLAY:
        os << "MUST_CERT_AND_REPLAY"; return;
    case TrxHandle::S_MUST_REPLAY_AM:
        os << "MUST_REPLAY_AM"; return;
    case TrxHandle::S_MUST_REPLAY_CM:
        os << "MUST_REPLAY_CM"; return;
    case TrxHandle::S_MUST_REPLAY:
        os << "MUST_REPLAY"; return;
    case TrxHandle::S_REPLAYING:
        os << "REPLAYING"; return;
    case TrxHandle::S_APPLYING:
        os << "APPLYING"; return;
    case TrxHandle::S_COMMITTING:
        os << "COMMITTING"; return;
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

inline
void galera::TrxHandle::print(std::ostream& os) const
{
    os << "source: "   << source_id()
       << " version: " << version()
       << " local: "   << local()
       << " state: "   << state()
       << " flags: "   << flags()
       << " conn_id: " << int64_t(conn_id())
       << " trx_id: "  << int64_t(trx_id())  // for readability
       << " tstamp: "  << timestamp();

}

std::ostream&
galera::operator<<(std::ostream& os, const TrxHandle& th)
{
    th.print(os); return os;
}

void galera::TrxHandleSlave::print(std::ostream& os) const
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
}

std::ostream&
galera::operator<<(std::ostream& os, const TrxHandleSlave& th)
{
    th.print(os); return os;
}


galera::TrxHandleMaster::Fsm::TransMap galera::TrxHandleMaster::trans_map_;
galera::TrxHandleSlave::Fsm::TransMap galera::TrxHandleSlave::trans_map_;


namespace galera {
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
    //  ||              |  MUST_CERT_AND_REPLAY --------------|         ^
    //  || SR Rollback  v           |               --------- | Cert OK  |
    //  | --------- ABORTING <-------               |         v          |
    //  |               |        Cert Fail          |   MUST_REPLAY_AM   |
    //  |               v                           |         |          |
    //  ----------> ROLLED_BACK                     |         v          |
    //                                              |-> MUST_REPLAY_CM   |
    //                                              |         |          |
    //                                              |         v          |
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
    add(TrxHandle::S_COMMITTING, TrxHandle::S_EXECUTING); // SR

    // BF aborted
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_CERT_AND_REPLAY);
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY_AM);
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY_CM);
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY);
    add(TrxHandle::S_MUST_ABORT, TrxHandle::S_ABORTING);

    // Cert and Replay
    add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_ABORTING);
    add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_MUST_REPLAY_AM);

    // Replay, interrupted before grabbing apply monitor
    add(TrxHandle::S_MUST_REPLAY_AM, TrxHandle::S_MUST_REPLAY_CM);

    // Replay, interrupted before grabbing commit monitor
    add(TrxHandle::S_MUST_REPLAY_CM, TrxHandle::S_MUST_REPLAY);

    // Replay, BF abort happens on application side after
    // commit monitor has been grabbed
    add(TrxHandle::S_MUST_REPLAY, TrxHandle::S_REPLAYING);

    // Replay stage
    add(TrxHandle::S_REPLAYING, TrxHandle::S_COMMITTED);

    // BF aborted and/or cert failed
    add(TrxHandle::S_ABORTING,  TrxHandle::S_ROLLED_BACK);

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
    //            |             |                 ^             |
    //            |             |Cert failed      |             |
    //            |             |                 |             |
    //            |             v                 |             v
    //            +-------> ABORTING -------------+   COMMITTED / ROLLED_BACK
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
    // Processing cert-failed and IST-skipped seqno
    add(TrxHandle::S_ABORTING,    TrxHandle::S_APPLYING);
    // Shortcut for BF'ed in release_rollback()
    add(TrxHandle::S_ABORTING,    TrxHandle::S_ROLLED_BACK);
    // Committing/Rolling back after applying
    add(TrxHandle::S_APPLYING,    TrxHandle::S_COMMITTING);
    // Commit finished
    add(TrxHandle::S_COMMITTING,  TrxHandle::S_COMMITTED);
    // Rollback cert-failed, apply-failed and IST-skipped seqno finished
    add(TrxHandle::S_COMMITTING,  TrxHandle::S_ROLLED_BACK);
}

static TransMapBuilder<TrxHandleMaster> master;
static TransMapBuilder<TrxHandleSlave> slave;

}

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
                               const wsrep_trx_meta_t& meta) const
{
    int err(0);

    const DataSetIn& ws(write_set_.dataset());
    void*  err_msg(NULL);
    size_t err_len(0);

    ws.rewind(); // make sure we always start from the beginning

    if (ws.count() > 0)
    {
        for (ssize_t i = 0; WSREP_CB_SUCCESS == err && i < ws.count(); ++i)
        {
            const gu::Buf& buf(ws.next());
            wsrep_buf_t const wb = { buf.ptr, size_t(buf.size) };

            err = apply_cb(recv_ctx, trx_flags_to_wsrep_flags(flags()), &wb,
                           &meta, &err_msg, &err_len);
        }
    }
    else
    {
        // Apply also zero sized write set to inform application side
        // about transaction meta data. This is done to avoid spreading
        // logic around in apply and commit callbacks with streaming
        // replication.
        wsrep_buf_t const wb = { NULL, 0 };
        err = apply_cb(recv_ctx, trx_flags_to_wsrep_flags(flags()), &wb, &meta,
                       &err_msg, &err_len);
        assert(NULL == err_msg);
        assert(0    == err_len);
    }

    if (gu_unlikely(0 != err))
    {
        std::ostringstream os;

        os << "Failed to apply app buffer: seqno: " << global_seqno()
           << ", code: " << err;

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

