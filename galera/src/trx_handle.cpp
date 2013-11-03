//
// Copyright (C) 2010-2013 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "uuid.hpp"
#include "galera_exception.hpp"

#include "gu_serialize.hpp"

const galera::TrxHandle::Params
galera::TrxHandle::Defaults(".", -1, KeySet::MAX_VERSION);

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
    os << "source: "  << th.source_id_
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
       << ")";

    if (th.write_set_in().annotated())
    {
        os << "\nAnnotation:\n";
        th.write_set_in().write_annotation(os);
        os << std::endl;
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


size_t galera::TrxHandle::Mac::serialize(gu::byte_t* buf, size_t buflen,
                                         size_t offset) const
{
    // header:
    // type: 1 byte
    // len:  1 byte
    return gu::serialize2(uint16_t(0), buf, buflen, offset);
}


size_t galera::TrxHandle::Mac::unserialize(const gu::byte_t* buf, size_t buflen,
                                           size_t offset)
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


size_t galera::TrxHandle::Mac::serial_size() const
{
    return 2; // sizeof(uint16_t); // Hm, isn't is somewhat short for mac?
}


size_t
galera::TrxHandle::serialize(gu::byte_t* buf, size_t buflen, size_t offset)const
{
    if (new_version()) { assert(0); } // we don't use serialize for that
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
        offset = mac_.serialize(buf, buflen, offset);
    }
    return offset;
}


size_t
galera::TrxHandle::unserialize(const gu::byte_t* const buf, size_t const buflen,
                               size_t offset)
{
    try
    {
        version_ = WriteSetNG::version(buf, buflen);

        switch (version_)
        {
        case 0:
        case 1:
        case 2:
            write_set_flags_ = buf[0];
            write_set_.set_version(version_);
            offset = 4;
            offset = galera::unserialize(buf, buflen, offset, source_id_);
            offset = gu::unserialize8(buf, buflen, offset, conn_id_);
            offset = gu::unserialize8(buf, buflen, offset, trx_id_);
            offset = gu::unserialize8(buf, buflen, offset, last_seen_seqno_);
            assert(last_seen_seqno_ >= 0);
            offset = gu::unserialize8(buf, buflen, offset, timestamp_);

            if (has_annotation())
            {
                offset = gu::unserialize4(buf, buflen, offset, annotation_);
            }

            if (has_mac())
            {
                offset = mac_.unserialize(buf, buflen, offset);
            }

            set_write_set_buffer(buf + offset, buflen - offset);

            break;
        case 3:
            write_set_in_.read_buf (buf, buflen);
            write_set_flags_ = wsng_flags_to_trx_flags(write_set_in_.flags());
            source_id_       = write_set_in_.source_id();
            conn_id_         = write_set_in_.conn_id();
            trx_id_          = write_set_in_.trx_id();
            if (write_set_in_.certified())
            {
                last_seen_seqno_ = WSREP_SEQNO_UNDEFINED;
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
            gu_throw_error(EPROTONOSUPPORT);
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


size_t
galera::TrxHandle::serial_size() const
{
    assert (new_version() == false);
    return (4 // hdr
            + galera::serial_size(source_id_)
            + 8 // serial_size(trx.conn_id_)
            + 8 // serial_size(trx.trx_id_)
            + 8 // serial_size(trx.last_seen_seqno_)
            + 8 // serial_size(trx.timestamp_)
            + (has_annotation() ? gu::serial_size4(annotation_) : 0)
            + (has_mac() ? mac_.serial_size() : 0));
}


void
galera::TrxHandle::apply (void*                   recv_ctx,
                          wsrep_apply_cb_t        apply_cb,
                          const wsrep_trx_meta_t& meta) const
{
    wsrep_cb_status_t err(WSREP_CB_SUCCESS);

    if (new_version())
    {
        const DataSetIn& ws(write_set_in_.dataset());

        ws.rewind(); // make sure we always start from the beginning

        for (ssize_t i = 0; WSREP_CB_SUCCESS == err && i < ws.count(); ++i)
        {
            gu::Buf buf = ws.next();

            err = apply_cb (recv_ctx, buf.ptr, buf.size,
                            trx_flags_to_wsrep_flags(flags()), &meta);
        }
    }
    else
    {
        const gu::byte_t* buf(write_set_buffer().first);
        const size_t buf_len(write_set_buffer().second);
        size_t offset(0);

        while (offset < buf_len && WSREP_CB_SUCCESS == err)
        {
            // Skip key segment
            std::pair<size_t, size_t> k(
                galera::WriteSet::segment(buf, buf_len, offset));
            offset = k.first + k.second;
            // Data part
            std::pair<size_t, size_t> d(
                galera::WriteSet::segment(buf, buf_len, offset));
            offset = d.first + d.second;

            err = apply_cb (recv_ctx, buf + d.first, d.second,
                            trx_flags_to_wsrep_flags(flags()), &meta);
        }

        assert(offset == buf_len);
    }

    if (gu_unlikely(err > 0))
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
galera::TrxHandle::unordered(void*                recv_ctx,
                             wsrep_unordered_cb_t cb) const
{
    if (new_version() && NULL != cb && write_set_in_.unrdset().count() > 0)
    {
        const DataSetIn& unrd(write_set_in_.unrdset());
        for (int i(0); i < unrd.count(); ++i)
        {
            const gu::Buf data = unrd.next();
            cb(recv_ctx, data.ptr, data.size);
        }
    }
}



