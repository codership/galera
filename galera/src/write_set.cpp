//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "write_set.hpp"
#include "serialization.hpp"

#include "gu_logger.hpp"


size_t galera::serialize(const WriteSet& ws,
                         gu::byte_t*     buf,
                         size_t          buf_len,
                         size_t          offset)
{
    offset = serialize<uint32_t>(ws.keys_, buf, buf_len, offset);
    offset = serialize<uint32_t>(ws.data_, buf, buf_len, offset);
    return offset;
}


size_t galera::unserialize(const gu::byte_t* buf,
                           size_t            buf_len,
                           size_t            offset,
                           WriteSet&         ws)
{
    ws.keys_.clear();
    offset = unserialize<uint32_t>(buf, buf_len, offset, ws.keys_);
    offset = unserialize<uint32_t>(buf, buf_len, offset, ws.data_);
    return offset;
}


size_t galera::serial_size(const WriteSet& ws)
{
    return (serial_size<uint32_t>(ws.keys_) + serial_size<uint32_t>(ws.data_));
}


void galera::WriteSet::append_key(const Key& key)
{
    const size_t hash(KeyHash()(key));

    std::pair<KeyRefMap::const_iterator, KeyRefMap::const_iterator>
        range(key_refs_.equal_range(hash));

    for (KeyRefMap::const_iterator i(range.first); i != range.second; ++i)
    {
        Key cmp;

        (void)galera::unserialize(&keys_[0], keys_.size(), i->second, cmp);

        if (key == cmp) return;
    }

    size_t key_size(serial_size(key));
    size_t offset(keys_.size());
    keys_.resize(offset + key_size);
    (void)galera::serialize(key, &keys_[0], keys_.size(), offset);
    (void)key_refs_.insert(std::make_pair(hash, offset));
}


void galera::WriteSet::get_keys(KeySequence& s) const
{
    size_t offset(0);
    while (offset < keys_.size())
    {
        Key key;
        if ((offset = unserialize(&keys_[0], keys_.size(), offset, key)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        s.push_back(key);
    }
    assert(offset == keys_.size());
}
