//
// Copyright (C) 2010-2015 Codership Oy <info@codership.com>
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
    os << " pa_range: " << write_set().pa_range();

    if (write_set().annotated())
    {
        os << "\nAnnotation:\n";
        write_set().write_annotation(os);
        os << std::endl;
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
    //  |  |            |               |            |            |     |
    //  |  |-------------------------------------------------------     |
    //  |  | BF Abort   ----------------|                               |
    //  |  v            |   Cert Fail                                   |
    //  | MUST_ABORT -----------------------------------------          |
    //  |               |           |                         |         |
    //  |      Pre Repl |           v                         |    REPLAYING
    //  |               |  MUST_CERT_AND_REPLAY -> CERTIFYING -         ^
    //  |               v           |               --------- | Cert OK  |
    //  |           ABORTING <-------               |         v          |
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
    add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_REPLAY_AM);
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
    add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_CERTIFYING);
    add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_ABORTING);

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
    add(TrxHandle::S_ABORTING,       TrxHandle::S_ROLLED_BACK);
}

template<>
TransMapBuilder<TrxHandleSlave>::TransMapBuilder()
    :
    trans_map_(TrxHandleSlave::trans_map_)
{
    //                                 Cert OK
    // 0 --> REPLICATING -> CERTIFYING ------> APPLYING -> COMMITTING
    //            |             |                               |
    //            |             v                               v
    //            |-------> ROLLED_BACK                    COMMITTED
    //

    // Enter in-order cert after replication
    add(TrxHandle::S_REPLICATING, TrxHandle::S_CERTIFYING);
    // Enter out-of-order cert after replication
    add(TrxHandle::S_REPLICATING, TrxHandle::S_ROLLED_BACK);
    // Applying after certification
    add(TrxHandle::S_CERTIFYING,  TrxHandle::S_APPLYING);
    // Roll back due to cert failure (maybe would be better to use ABORTING?)
    add(TrxHandle::S_CERTIFYING,  TrxHandle::S_ROLLED_BACK);
    // Committing after applying
    add(TrxHandle::S_APPLYING,    TrxHandle::S_COMMITTING);
    // Commit finished
    add(TrxHandle::S_COMMITTING,  TrxHandle::S_COMMITTED);
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
                 << "This trx should not have been replicated at all: " << *this;
    }
}

size_t
galera::TrxHandleSlave::unserialize(const gu::byte_t* const buf,
                                    size_t const buflen,
                                    size_t offset)
{
    try
    {
        version_ = WriteSetNG::version(buf, buflen);

        switch (version_)
        {
        case WriteSetNG::VER3:
        case WriteSetNG::VER4:
            write_set_.read_buf (buf, buflen);
            assert(version_ == write_set_.version());
            write_set_flags_ = ws_flags_to_trx_flags(write_set_.flags());
            source_id_       = write_set_.source_id();
            conn_id_         = write_set_.conn_id();
            trx_id_          = write_set_.trx_id();
#ifndef NDEBUG
            write_set_.verify_checksum();
            assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);
#endif
            if (write_set_.certified())
            {
                assert(!local_);
                assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);

                global_seqno_  = write_set_.seqno();
                depends_seqno_ = global_seqno_ - write_set_.pa_range();
                assert(depends_seqno_ < global_seqno_);
                assert(depends_seqno_ >= 0);
                certified_ = true;
            }
            else
            {

                last_seen_seqno_ = write_set_.last_seen();
                assert(last_seen_seqno_ >= 0);

                if (gu_likely(version_) >= WriteSetNG::VER4)
                {
                    depends_seqno_ = last_seen_seqno_ - write_set_.pa_range();
                }
                else
                {
                    assert(WSREP_SEQNO_UNDEFINED == depends_seqno_);
                }
            }

            timestamp_ = write_set_.timestamp();

            sanity_checks();

            break;
        default:
            gu_throw_error(EPROTONOSUPPORT) << "Unsupported WS version: "
                                            << version_;
        }

        return buflen;
    }
    catch (gu::Exception& e)
    {
        GU_TRACE(e);

        log_fatal << "Writeset deserialization failed: " << e.what()
                  << std::endl << "WS flags:      " << write_set_flags_
                  << std::endl << "Trx proto:     " << version_
                  << std::endl << "Trx source:    " << source_id_
                  << std::endl << "Trx conn_id:   " << conn_id_
                  << std::endl << "Trx trx_id:    " << trx_id_
                  << std::endl << "Trx last_seen: " << last_seen_seqno_;
        throw;
    }
}


void
galera::TrxHandleSlave::apply (void*                   recv_ctx,
                               wsrep_apply_cb_t        apply_cb,
                               const wsrep_trx_meta_t& meta) const
{
    wsrep_cb_status_t err(WSREP_CB_SUCCESS);

    assert(version() >= WS_NG_VERSION);

    const DataSetIn& ws(write_set_.dataset());

    ws.rewind(); // make sure we always start from the beginning

    for (ssize_t i = 0; WSREP_CB_SUCCESS == err && i < ws.count(); ++i)
    {
        gu::Buf buf = ws.next();

        err = apply_cb (recv_ctx, buf.ptr, buf.size,
                        trx_flags_to_wsrep_flags(flags()), &meta);
    }

    if (gu_unlikely(err != WSREP_CB_SUCCESS))
    {
        std::ostringstream os;

        os << "Failed to apply app buffer: seqno: " << global_seqno()
           << ", status: " << err;

        galera::ApplyException ae(os.str(), err);

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
    assert(version() >= WS_NG_VERSION);

    if (NULL != cb && write_set_.unrdset().count() > 0)
    {
        const DataSetIn& unrd(write_set_.unrdset());
        for (int i(0); i < unrd.count(); ++i)
        {
            const gu::Buf data = unrd.next();
            cb(recv_ctx, data.ptr, data.size);
        }
    }
}


void
galera::TrxHandleSlave::destroy_local(void*  ptr)
{
    assert(ptr);
    (static_cast<TrxHandleMaster*>(ptr))->~TrxHandleMaster();
}

