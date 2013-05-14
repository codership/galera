//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "uuid.hpp"

#include "gu_serialize.hpp"

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
    case TrxHandle::S_CERTIFYING:
        return (os << "CERTIFYING");
    case TrxHandle::S_MUST_CERT_AND_REPLAY:
        return (os << "MUST_CERT_AND_REPLAY");
    case TrxHandle::S_MUST_REPLAY_AM:
        return (os << "MUST_REPLAY_AM");
    case TrxHandle::S_MUST_REPLAY_CM:
        return (os << "MUST_REPLAY_CM");
    case TrxHandle::S_MUST_REPLAY:
        return (os << "MUST_REPLAY");
    case TrxHandle::S_REPLAYING:
        return (os << "REPLAYING");
    case TrxHandle::S_APPLYING:
        return (os << "APPLYING");
    case TrxHandle::S_COMMITTING:
        return (os << "COMMITTING");
    case TrxHandle::S_COMMITTED:
        return (os << "COMMITTED");
    case TrxHandle::S_ROLLED_BACK:
        return (os << "ROLLED_BACK");
    }
    gu_throw_fatal << "invalid state " << static_cast<int>(s);
}


std::ostream&
galera::operator<<(std::ostream& os, const TrxHandle& th)
{
    return (os << "source: "  << th.source_id_
            << " version: "   << th.version_
            << " local: "     << th.local_
            << " state: "     << th.state_()
            << " flags: "     << th.write_set_flags_
            << " conn_id: "   << int64_t(th.conn_id_)
            << " trx_id: "    << int64_t(th.trx_id_) // for readability
            << " seqnos (l: " << th.local_seqno_
            << ", g: "        << th.global_seqno_
            << ", s: "        << th.last_seen_seqno_
            << ", d: "        << th.depends_seqno_
            << ", ts: "       << th.timestamp_
            << ")");
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

        add(TrxHandle::S_EXECUTING, TrxHandle::S_REPLICATING);
        add(TrxHandle::S_EXECUTING, TrxHandle::S_ROLLED_BACK);

        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY_AM);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY_CM);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_REPLAY);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_MUST_ABORT);
        add(TrxHandle::S_MUST_ABORT, TrxHandle::S_ABORTING);

        add(TrxHandle::S_ABORTING, TrxHandle::S_ROLLED_BACK);

        add(TrxHandle::S_REPLICATING, TrxHandle::S_CERTIFYING);
        add(TrxHandle::S_REPLICATING, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_REPLICATING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_ABORT);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_APPLYING);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_CERT_AND_REPLAY);
        add(TrxHandle::S_CERTIFYING, TrxHandle::S_MUST_REPLAY_AM); // trx replay

        add(TrxHandle::S_APPLYING, TrxHandle::S_MUST_ABORT);
        add(TrxHandle::S_APPLYING, TrxHandle::S_COMMITTING);

        add(TrxHandle::S_COMMITTING, TrxHandle::S_COMMITTED);
        add(TrxHandle::S_COMMITTING, TrxHandle::S_MUST_ABORT);

        add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_CERTIFYING);
        add(TrxHandle::S_MUST_CERT_AND_REPLAY, TrxHandle::S_ABORTING);

        add(TrxHandle::S_MUST_REPLAY_AM, TrxHandle::S_MUST_REPLAY_CM);
        add(TrxHandle::S_MUST_REPLAY_CM, TrxHandle::S_MUST_REPLAY);
        add(TrxHandle::S_MUST_REPLAY, TrxHandle::S_REPLAYING);
        add(TrxHandle::S_REPLAYING, TrxHandle::S_COMMITTED);
    }
} trans_map_builder_;


size_t galera::serialize(const TrxHandle::Mac& mac, gu::byte_t* buf,
                                size_t buflen, size_t offset)
{
    // header:
    // type: 1 byte
    // len:  1 byte
    return gu::serialize2(uint16_t(0), buf, buflen, offset);
}


size_t galera::unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                           TrxHandle::Mac& mac)
{
    uint16_t hdr;
    offset = gu::unserialize2(buf, buflen, offset, hdr);
    switch ((hdr >> 8) & 0xff)
    {
    case 0:
        break;
    default:
        log_warn << "unrecognized mac type" << ((hdr >> 8) & 0xff);
    }
    // skip over the body
    offset += (hdr & 0xff);
    return offset;
}


size_t galera::serial_size(const TrxHandle::Mac& mac)
{
    return 2; // sizeof(uint16_t); // Hm, isn't is somewhat short for mac?
}


size_t
galera::TrxHandle::serialize(gu::byte_t* buf, size_t buflen, size_t offset)const
{
    uint32_t hdr((version_ << 24) | (write_set_flags_ & 0xff));
    offset = gu::serialize4(hdr, buf, buflen, offset);
    offset = galera::serialize(source_id_, buf, buflen, offset);
    offset = gu::serialize8(conn_id_, buf, buflen, offset);
    offset = gu::serialize8(trx_id_, buf, buflen, offset);
    offset = gu::serialize8(last_seen_seqno_, buf, buflen, offset);
    offset = gu::serialize8(timestamp_, buf, buflen, offset);
    if (has_annotation())
    {
        offset = gu::serialize4(annotation_, buf, buflen, offset);
    }
    if (has_mac())
    {
        offset = galera::serialize(mac_, buf, buflen, offset);
    }
    return offset;
}


size_t
galera::TrxHandle::unserialize(const gu::byte_t* buf, size_t buflen,
                               size_t offset)
{
    uint32_t hdr;

    try
    {
        offset = gu::unserialize4(buf, buflen, offset, hdr);
        write_set_flags_ = hdr & 0xff;
        version_ = hdr >> 24;
        write_set_.set_version(version_);

        switch (version_)
        {
        case 0:
        case 1:
        case 2:
        case 3:
            offset = galera::unserialize(buf, buflen, offset, source_id_);
            offset = gu::unserialize8(buf, buflen, offset, conn_id_);
            offset = gu::unserialize8(buf, buflen, offset, trx_id_);
            offset = gu::unserialize8(buf, buflen, offset, last_seen_seqno_);
            offset = gu::unserialize8(buf, buflen, offset, timestamp_);

            if (has_annotation())
            {
                offset = gu::unserialize4(buf, buflen, offset, annotation_);
            }
            if (has_mac())
            {
                offset = galera::unserialize(buf, buflen, offset, mac_);
            }
            break;
        default:
            gu_throw_error(EPROTONOSUPPORT);
        }
        return offset;
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


size_t
galera::TrxHandle::serial_size() const
{
    return (4 // hdr
            + galera::serial_size(source_id_)
            + 8 // serial_size(trx.conn_id_)
            + 8 // serial_size(trx.trx_id_)
            + 8 // serial_size(trx.last_seen_seqno_)
            + 8 // serial_size(trx.timestamp_)
            + (has_annotation() ? gu::serial_size4(annotation_) : 0)
            + (has_mac() ? galera::serial_size(mac_) : 0));
}


#if 0 // REMOVE
size_t galera::serialize(const TrxHandle& trx, gu::byte_t* buf,
                         size_t buflen, size_t offset)
{
    uint32_t hdr((trx.version_ << 24) | (trx.write_set_flags_ & 0xff));
    offset = gu::serialize4(hdr, buf, buflen, offset);
    offset = serialize(trx.source_id_, buf, buflen, offset);
    offset = gu::serialize8(trx.conn_id_, buf, buflen, offset);
    offset = gu::serialize8(trx.trx_id_, buf, buflen, offset);
    offset = gu::serialize8(trx.last_seen_seqno_, buf, buflen, offset);
    offset = gu::serialize8(trx.timestamp_, buf, buflen, offset);
    if (trx.has_annotation())
    {
        offset = gu::serialize4(trx.annotation_, buf, buflen, offset);
    }
    if (trx.has_mac())
    {
        offset = serialize(trx.mac_, buf, buflen, offset);
    }
    return offset;
}


size_t galera::unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                           TrxHandle& trx)
{
    uint32_t hdr;

    try
    {
        offset = gu::unserialize4(buf, buflen, offset, hdr);
        trx.write_set_flags_ = hdr & 0xff;
        trx.version_ = hdr >> 24;
        trx.write_set_.set_version(trx.version_);

        switch (trx.version_)
        {
        case 0:
        case 1:
        case 2:
            offset = unserialize(buf, buflen, offset, trx.source_id_);
            offset = gu::unserialize8(buf, buflen, offset, trx.conn_id_);
            offset = gu::unserialize8(buf, buflen, offset, trx.trx_id_);
            offset = gu::unserialize8(buf, buflen, offset, trx.last_seen_seqno_);
            offset = gu::unserialize8(buf, buflen, offset, trx.timestamp_);
            if (trx.has_annotation())
            {
                offset = gu::unserialize4(buf, buflen, offset,
                                          trx.annotation_);
            }
            if (trx.has_mac())
            {
                offset = unserialize(buf, buflen, offset, trx.mac_);
            }
            break;
        default:
            gu_throw_error(EPROTONOSUPPORT);
        }
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
    return (4 // hdr
            + serial_size(trx.source_id_)
            + 8 // serial_size(trx.conn_id_)
            + 8 // serial_size(trx.trx_id_)
            + 8 // serial_size(trx.last_seen_seqno_)
            + 8 // serial_size(trx.timestamp_)
            + (trx.has_annotation() ?
               gu::serial_size4(trx.annotation_) : 0)
            + (trx.has_mac() ? serial_size(trx.mac_) : 0));
}

#endif // REMOVE
