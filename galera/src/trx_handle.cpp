//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "serialization.hpp"

#include "uuid.hpp"

std::ostream& galera::operator<<(std::ostream& os, TrxHandle::State s)
{
    switch (s)
    {
    case TrxHandle::S_EXECUTING:
        return (os << "EXECUTING");
    case TrxHandle::S_MUST_ABORT:
        return (os << "MUST_ABORT");
    case TrxHandle::S_ABORTING:
        return (os << "ABORTING");
    case TrxHandle::S_REPLICATING:
        return (os << "REPLICATING");
    case TrxHandle::S_REPLICATED:
        return (os << "REPLICATED");
    case TrxHandle::S_CERTIFYING:
        return (os << "CERTIFYING");
    case TrxHandle::S_CERTIFIED:
        return (os << "CERTIFIED");
    case TrxHandle::S_MUST_CERT_AND_REPLAY:
        return (os << "MUST_CERT_AND_REPLAY");
    case TrxHandle::S_MUST_REPLAY:
        return (os << "MUST_REPLAY");
    case TrxHandle::S_REPLAYING:
        return (os << "REPLAYING");
    case TrxHandle::S_REPLAYED:
        return (os << "REPLAYED");
    case TrxHandle::S_APPLYING:
        return (os << "APPLYING");
    case TrxHandle::S_COMMITTED:
        return (os << "COMMITTED");
    case TrxHandle::S_ROLLED_BACK:
        return (os << "ROLLED_BACK");
    }
    gu_throw_fatal << "invalid state " << static_cast<int>(s);
    throw;
}


std::ostream&
galera::operator<<(std::ostream& os, const TrxHandle& th)
{
    os << "source: " << th.source_id_
       << " state: " << th.state_()
       << " flags: " << th.write_set_flags_
       << " conn_id: " << th.conn_id_
       << " trx_id: " << th.trx_id_
       << " seqnos (l: "  << th.local_seqno_
       << ", g: " << th.global_seqno_
       << ", s: " << th.last_seen_seqno_
       << ", d: " << th.last_depends_seqno_
       << ") ";
    os << "statements:\n";
    for (StatementSequence::const_iterator i = th.write_set_.get_queries().begin(); i != th.write_set_.get_queries().end(); ++i)
    {
        os << "\t" << *i << "\n";
    }
    return os;
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

        add(TrxHandle::S_EXECUTING, TrxHandle::S_MUST_ABORT);
//        add(TrxHandle::S_EXECUTING, TrxHandle::S_ABORTING);
        add(TrxHandle::S_EXECUTING, TrxHandle::S_REPLICATING);
        add(TrxHandle::S_EXECUTING, TrxHandle::S_APPLYING);
        add(TrxHandle::S_EXECUTING, TrxHandle::S_ROLLED_BACK);

        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_ABORT);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_ABORTING);

        add(TrxHandle::S_ABORTING, TrxHandle::S_ROLLED_BACK);

        add(TrxHandle::S_REPLICATING, TrxHandle::S_REPLICATED);
        add(TrxHandle::S_REPLICATING, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_REPLICATING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_REPLICATED, TrxHandle::S_CERTIFYING);

        add(TrxHandle::S_CERTIFYING, TrxHandle::S_CERTIFIED);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_CERTIFIED, TrxHandle::S_EXECUTING);
        add(TrxHandle::S_CERTIFIED, TrxHandle::S_APPLYING);
        add(TrxHandle::S_CERTIFIED, TrxHandle::S_REPLAYING);
        add(TrxHandle::S_CERTIFIED, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_APPLYING, TrxHandle::S_COMMITTED);

        add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_CERTIFYING);
        add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_ABORTING);

        add(TrxHandle::S_MUST_REPLAY, TrxHandle::S_REPLAYING);

        add(TrxHandle::S_REPLAYING, TrxHandle::S_REPLAYED);
        add(TrxHandle::S_REPLAYING, TrxHandle::S_ABORTING);

        add(TrxHandle::S_REPLAYED, TrxHandle::S_COMMITTED);

    }
} trans_map_builder_;


size_t galera::serialize(const TrxHandle& trx, gu::byte_t* buf,
                         size_t buflen, size_t offset)
{
    uint32_t hdr((trx.version_ << 24) | (trx.write_set_flags_ & 0xff));
    offset = serialize(hdr, buf, buflen, offset);
    offset = serialize(trx.source_id_, buf, buflen, offset);
    offset = serialize(trx.conn_id_, buf, buflen, offset);
    offset = serialize(trx.trx_id_, buf, buflen, offset);
    offset = serialize(trx.last_seen_seqno_, buf, buflen, offset);
    return offset;
}


size_t galera::unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                           TrxHandle& trx)
{
    uint32_t hdr;
    try
    {
        offset = unserialize(buf, buflen, offset, hdr);
        trx.write_set_flags_ = hdr & 0xff;
        trx.version_ = hdr >> 24;

        if (trx.version_ != 0) gu_throw_error(EPROTONOSUPPORT);

        offset = unserialize(buf, buflen, offset, trx.source_id_);
        offset = unserialize(buf, buflen, offset, trx.conn_id_);
        offset = unserialize(buf, buflen, offset, trx.trx_id_);
        offset = unserialize(buf, buflen, offset, trx.last_seen_seqno_);
        return offset;
    }
    catch (gu::Exception& e)
    {
        GU_TRACE(e);

        log_fatal << "Writeset deserialization failed: " << e.what()
                  << std::endl << "WS flags:      " << trx.write_set_flags_
                  << std::endl << "Trx proto:     " << trx.version_
                  << std::endl << "Trx source:    " << trx.source_id_
                  << std::endl << "Trx conn_id:   " << trx.conn_id_
                  << std::endl << "Trx trx_id:    " << trx.trx_id_
                  << std::endl << "Trx last_seen: " << trx.last_seen_seqno_;

        throw;
    }
}


size_t galera::serial_size(const TrxHandle& trx)
{
    return (sizeof(uint32_t) // hdr
            + serial_size(trx.source_id_)
            + serial_size(trx.conn_id_)
            + serial_size(trx.trx_id_)
            + serial_size(trx.last_seen_seqno_));
}

