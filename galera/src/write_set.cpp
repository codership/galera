//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "write_set.hpp"
#include "serialization.hpp"

#include "gu_logger.hpp"


size_t galera::serialize(const RowId& row_key,
                         gu::byte_t*   buf,
                         size_t        buf_len,
                         size_t        offset)
{
    offset = serialize<uint16_t>(row_key.table_, row_key.table_len_,
                                 buf, buf_len, offset);
    offset = serialize<uint16_t>(row_key.key_, row_key.key_len_, buf,
                                 buf_len, offset);
    return offset;
}


size_t galera::unserialize(const gu::byte_t* buf,
                           size_t            buf_len,
                           size_t            offset,
                           RowId&           row_key)
{
    offset = unserialize<uint16_t>(buf, buf_len, offset, row_key.table_,
                                   row_key.table_len_);
    offset = unserialize<uint16_t>(buf, buf_len, offset, row_key.key_,
                                   row_key.key_len_);
    return offset;
}


size_t galera::serial_size(const RowId& row_key)
{
    return (serial_size<uint16_t>(row_key.table_, row_key.table_len_)
            + serial_size<uint16_t>(row_key.key_, row_key.key_len_));
}


size_t galera::serialize(const WriteSet& ws,
                         gu::byte_t*     buf,
                         size_t          buf_len,
                         size_t          offset)
{
    offset = serialize<uint32_t>(
        &ws.row_ids_[0], ws.row_ids_.size(), buf, buf_len, offset);
    offset = serialize<uint32_t>(ws.data_, buf, buf_len, offset);
    return offset;
}


size_t galera::unserialize(const gu::byte_t* buf,
                           size_t            buf_len,
                           size_t            offset,
                           WriteSet&         ws)
{
    ws.row_ids_.clear();
    offset = unserialize<uint32_t>(
        buf, buf_len, offset, ws.row_ids_);
    offset = unserialize<uint32_t>(buf, buf_len, offset, ws.data_);
    return offset;
}


size_t galera::serial_size(const WriteSet& ws)
{
    return (serial_size<uint32_t>(&ws.row_ids_[0], ws.row_ids_.size())
            + serial_size<uint32_t>(ws.data_));
}


void galera::WriteSet::append_row_id(const void* table,
                                     size_t      table_len,
                                     const void* key,
                                     size_t      key_len)
{
    RowId rk(table, table_len, key, key_len);
    const size_t hash(rk.get_hash());

    std::pair<RowIdRefMap::const_iterator, RowIdRefMap::const_iterator>
        range(row_id_refs_.equal_range(hash));

    for (RowIdRefMap::const_iterator i = range.first; i != range.second; ++i)
    {
        RowId cmp;

        (void)galera::unserialize(&row_ids_[0], row_ids_.size(), i->second, cmp);

        if (rk == cmp) return;
    }

    size_t rk_size(serial_size(rk));
    size_t offset(row_ids_.size());
    row_ids_.resize(offset + rk_size);
    (void)galera::serialize(rk, &row_ids_[0], row_ids_.size(), offset);
    (void)row_id_refs_.insert(std::make_pair(hash, offset));
}


void galera::WriteSet::get_row_ids(RowIdSequence& s) const
{
    size_t offset(0);
    while (offset < row_ids_.size())
    {
        RowId rk;
        if ((offset = unserialize(&row_ids_[0], row_ids_.size(), offset, rk)) == 0)
        {
            gu_throw_fatal << "failed to unserialize row key";
        }
        s.push_back(rk);
    }
    assert(offset == row_ids_.size());
}
