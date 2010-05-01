//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "galera_write_set.hpp"
#include "serialization.hpp"

using namespace std;
using namespace gu;

size_t galera::serialize(const RowKey& row_key, 
                         gu::byte_t*   buf,
                         size_t        buf_len,
                         size_t        offset)
{
    offset = serialize<uint16_t>(row_key.dbtable_, buf, buf_len, offset);
    offset = serialize<uint16_t>(row_key.key_, buf, buf_len, offset);
    offset = serialize(static_cast<gu::byte_t>(row_key.action_), buf, buf_len, offset);
    return offset;
}

size_t galera::unserialize(const gu::byte_t* buf, size_t buf_len, size_t offset,
                           RowKey& row_key)
{
    row_key.clear();
    offset = unserialize<uint16_t>(buf, buf_len, offset, row_key.dbtable_);
    offset = unserialize<uint16_t>(buf, buf_len, offset, row_key.key_);
    byte_t act_b;
    offset = unserialize(buf, buf_len, offset, act_b);
    row_key.action_ = static_cast<int>(act_b);
    return offset;
}

size_t galera::serial_size(const RowKey& row_key)
{
    return (serial_size<uint16_t>(row_key.dbtable_) 
            + serial_size<uint16_t>(row_key.key_) 
            + serial_size(byte_t()));
}


size_t galera::serialize(const GaleraWriteSet& ws, gu::byte_t* buf, 
                         size_t buf_len, size_t offset)
{
    uint32_t hdr(ws.type_ | (ws.level_ << 8));
    offset = serialize(hdr, buf, buf_len, offset);
    offset = serialize<GaleraWriteSet::QuerySequence, uint32_t>(
        ws.queries_, buf, buf_len, offset);
    offset = serialize<GaleraWriteSet::RowKeySequence, uint32_t>(
        ws.keys_, buf, buf_len, offset);
    offset = serialize<uint32_t>(ws.rbr_, buf, buf_len, offset);
    return offset;
}

size_t galera::unserialize(const gu::byte_t* buf, size_t buf_len,
                           size_t offset, GaleraWriteSet& ws)
{
    uint32_t hdr;
    offset = unserialize(buf, buf_len, offset, hdr);
    ws.type_ = static_cast<enum wsdb_ws_type>(hdr & 0xff);
    ws.level_ = static_cast<enum wsdb_ws_level>((hdr >> 8) & 0xff);
    
    offset = unserialize<GaleraWriteSet::QuerySequence, uint32_t>(
        buf, buf_len, offset, ws.queries_);
    offset = unserialize<GaleraWriteSet::RowKeySequence, uint32_t>(
        buf, buf_len, offset, ws.keys_);
    offset = unserialize<uint32_t>(buf, buf_len, offset, ws.rbr_);
    return offset;
}

size_t galera::serial_size(const GaleraWriteSet& ws)
{
    return (serial_size(uint32_t()) 
            + serial_size<GaleraWriteSet::QuerySequence, uint32_t>(ws.queries_)
            + serial_size<GaleraWriteSet::RowKeySequence, uint32_t>(ws.keys_)
            + serial_size<uint32_t>(ws.rbr_));
}

void galera::GaleraWriteSet::serialize(Buffer& buf) const
{
    buf.resize(serial_size(*this));
    if (galera::serialize(*this, &buf[0], buf.size(), 0) == 0)
    {
        gu_throw_fatal << "failed to serialize write set";
    }
}
