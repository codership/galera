//
// Copyright (C) 2010-2016 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "galera_exception.hpp"

#include <gu_serialize.hpp>
#include <gu_uuid.hpp>

const galera::TrxHandle::Params
galera::TrxHandle::Defaults(".", -1, KeySet::MAX_VERSION);

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
    case TrxHandle::S_ROLLING_BACK:
        os << "ROLLING_BACK"; return;
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

void galera::TrxHandle::print_state_history(std::ostream& os) const
{
    const std::vector<TrxHandle::State>& hist(state_.history());
    for (size_t i(0); i < hist.size(); ++i)
    {
        os << hist[i] << "->";
    }
}

void
galera::TrxHandle::print(std::ostream& os) const
{
    os << "source: "     << source_id_
       << " version: "   << version_
       << " local: "     << local_
       << " state: "     << state_()
       << " flags: "     << write_set_flags_
       << " conn_id: "   << int64_t(conn_id_)
       << " trx_id: "    << int64_t(trx_id_) // for readability
       << " seqnos (l: " << local_seqno_
       << ", g: "        << global_seqno_
       << ", s: "        << last_seen_seqno_
       << ", d: "        << depends_seqno_
       << ", ts: "       << timestamp_
       << ")";

    if (!skip_event())
    {
        if (write_set_in().size() > 0)
        {
            os << " WS pa_range: " << write_set_in().pa_range();

            if (write_set_in().annotated())
            {
                os << "\nAnnotation:\n";
                write_set_in().write_annotation(os);
                os << std::endl;
            }
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
galera::operator<<(std::ostream& os, const TrxHandle& th)
{
    th.print(os); return os;
}


galera::TrxHandle::Fsm::TransMap galera::TrxHandle::trans_map_;

static class TransMapBuilder
{
public:
    void add(galera::TrxHandle::State from, galera::TrxHandle::State to)
    {
        using galera::TrxHandle;
        using std::make_pair;
        typedef TrxHandle::Transition Transition;
        typedef TrxHandle::Fsm::TransAttr TransAttr;
        TrxHandle::Fsm::TransMap& trans_map(TrxHandle::trans_map_);
        trans_map.insert_unique(make_pair(Transition(from, to), TransAttr()));
    }

    TransMapBuilder()
    {
        using galera::TrxHandle;

        add(TrxHandle::S_EXECUTING, TrxHandle::S_REPLICATING);
        add(TrxHandle::S_EXECUTING, TrxHandle::S_ROLLED_BACK);
        add(TrxHandle::S_EXECUTING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY_AM);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY_CM);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_ABORTING);

        add(TrxHandle::S_ABORTING, TrxHandle::S_ROLLING_BACK);// BF in apply mon
        add(TrxHandle::S_ABORTING, TrxHandle::S_ROLLED_BACK); // BF in repl

        add(TrxHandle::S_ROLLING_BACK, TrxHandle::S_ROLLED_BACK);

        add(TrxHandle::S_REPLICATING, TrxHandle::S_CERTIFYING);
        add(TrxHandle::S_REPLICATING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_CERTIFYING, TrxHandle::S_APPLYING);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_ABORTING);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_APPLYING, TrxHandle::S_MUST_ABORT);
        add(TrxHandle::S_APPLYING, TrxHandle::S_COMMITTING);

        add(TrxHandle::S_COMMITTING, TrxHandle::S_MUST_ABORT);
        add(TrxHandle::S_COMMITTING, TrxHandle::S_COMMITTED);
        add(TrxHandle::S_COMMITTING, TrxHandle::S_ROLLED_BACK); // apply error

        add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_ABORTING);
        add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_MUST_REPLAY_AM);

        add(TrxHandle::S_MUST_REPLAY_AM, TrxHandle::S_MUST_REPLAY_CM);
        add(TrxHandle::S_MUST_REPLAY_CM, TrxHandle::S_MUST_REPLAY);
        add(TrxHandle::S_MUST_REPLAY, TrxHandle::S_REPLAYING);
        add(TrxHandle::S_REPLAYING, TrxHandle::S_COMMITTED);
    }
} trans_map_builder_;


size_t
galera::TrxHandle::unserialize(const gu::byte_t* const buf, size_t const buflen,
                               size_t offset)
{
    try
    {
        version_ = WriteSetNG::version(buf, buflen);

        switch (version_)
        {
        case 3:
            write_set_in_.read_buf (buf, buflen);
            assert(write_set_in_.flags() != 0);
            write_set_flags_ = ws_flags_to_trx_flags(write_set_in_.flags());
            source_id_       = write_set_in_.source_id();
            conn_id_         = write_set_in_.conn_id();
            trx_id_          = write_set_in_.trx_id();

#ifndef NDEBUG
            write_set_in_.verify_checksum();
            if (local_)
                assert(write_set_in_.last_seen() == last_seen_seqno_);
            else
                assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);
#endif
            if (write_set_in_.certified())
            {
                assert(!local_);
                assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);
                write_set_flags_ |= F_PREORDERED;
            }
            else
            {
                last_seen_seqno_ = write_set_in_.last_seen();
                assert(last_seen_seqno_ >= 0);
            }

            timestamp_       = write_set_in_.timestamp();
            break;
        default:
            gu_throw_error(EPROTONOSUPPORT) <<"Unsupported WS version: "
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
galera::TrxHandle::apply (void*                   recv_ctx,
                          wsrep_apply_cb_t        apply_cb,
                          const wsrep_trx_meta_t& meta,
                          wsrep_bool_t&           exit_loop)
{
    uint32_t const wsrep_flags
        (trx_flags_to_wsrep_flags(flags()) | WSREP_FLAG_TRX_START);

    int err(0);

    const DataSetIn& ws(write_set_in_.dataset());
    void*  err_msg(NULL);
    size_t err_len(0);

    ws.rewind(); // make sure we always start from the beginning

    wsrep_ws_handle_t const wh = { trx_id(), this };

    if (ws.count() > 0)
    {
        for (ssize_t i = 0; WSREP_CB_SUCCESS == err && i < ws.count(); ++i)
        {
            gu::Buf buf(ws.next());
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
galera::TrxHandle::unordered(void*                recv_ctx,
                             wsrep_unordered_cb_t cb) const
{
    if (NULL != cb && write_set_in_.unrdset().count() > 0)
    {
        const DataSetIn& unrd(write_set_in_.unrdset());
        for (int i(0); i < unrd.count(); ++i)
        {
            const gu::Buf& data(unrd.next());
            wsrep_buf_t const wb = { data.ptr, size_t(data.size) };
            cb(recv_ctx, &wb);
        }
    }
}

