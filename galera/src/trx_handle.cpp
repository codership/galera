//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"
#include "serialization.hpp"

#include "gu_uuid.h"

std::ostream&
galera::operator<<(std::ostream& os, const TrxHandle& th)
{
    char uuid_buf[GU_UUID_STR_LEN + 1];
    gu_uuid_t gu_uuid;
    memcpy(gu_uuid.data, th.source_id_.uuid, sizeof(gu_uuid.data));

    sprintf(uuid_buf, GU_UUID_FORMAT, GU_UUID_ARGS(&gu_uuid));
    os << "source: " << uuid_buf
       << " state: " << th.state_
       << " wst: " << th.write_set_type_
       << " flags: " << th.write_set_flags_
       << " conn_id: " << th.conn_id_
       << " trx_id: " << th.trx_id_
       << " seqnos (l: "  << th.local_seqno_
       << ", g: " << th.global_seqno_
       << ", s: " << th.last_seen_seqno_
       << ", d: " << th.last_depends_seqno_
       << ')';

    return os;
}


size_t galera::serialize(const TrxHandle& trx, gu::byte_t* buf,
                         size_t buflen, size_t offset)
{
    uint32_t hdr((trx.write_set_type_ << 8) | (trx.write_set_flags_ & 0xff));
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
    offset = unserialize(buf, buflen, offset, hdr);
    trx.write_set_flags_ = hdr & 0xff;
    trx.write_set_type_ = static_cast<enum wsdb_ws_type>((hdr >> 8) & 0xff);
    offset = unserialize(buf, buflen, offset, trx.source_id_);
    offset = unserialize(buf, buflen, offset, trx.conn_id_);
    offset = unserialize(buf, buflen, offset, trx.trx_id_);
    offset = unserialize(buf, buflen, offset, trx.last_seen_seqno_);
    return offset;
}


size_t galera::serial_size(const TrxHandle& trx)
{
    return (sizeof(uint32_t) // hdr
            + serial_size(trx.source_id_)
            + serial_size(trx.conn_id_)
            + serial_size(trx.trx_id_)
            + serial_size(trx.last_seen_seqno_));
}
