//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
//


#include "write_set.hpp"

#include "gu_serialize.hpp"
#include "gu_logger.hpp"


size_t galera::serialize(const WriteSet& ws,
                         gu::byte_t*     buf,
                         size_t          buf_len,
                         size_t          offset)
{
    offset = gu::serialize4(ws.keys_, buf, buf_len, offset);
    offset = gu::serialize4(ws.data_, buf, buf_len, offset);
    return offset;
}


size_t galera::unserialize(const gu::byte_t* buf,
                           size_t            buf_len,
                           size_t            offset,
                           WriteSet&         ws)
{
    ws.keys_.clear();
    offset = gu::unserialize4(buf, buf_len, offset, ws.keys_);
    offset = gu::unserialize4(buf, buf_len, offset, ws.data_);
    return offset;
}


size_t galera::serial_size(const WriteSet& ws)
{
    return (gu::serial_size4(ws.keys_) + gu::serial_size4(ws.data_));
}

std::pair<size_t, size_t>
galera::WriteSet::segment(const gu::byte_t* buf, size_t buf_len, size_t offset)
{
    uint32_t data_len;
    offset = gu::unserialize4(buf, buf_len, offset, data_len);
    if (offset + data_len > buf_len) gu_throw_error(EMSGSIZE);
    return std::pair<size_t, size_t>(offset, data_len);
}

size_t galera::WriteSet::keys(const gu::byte_t* buf,
                              size_t buf_len, size_t offset, int version,
                              KeySequence& ks)
{
    std::pair<size_t, size_t> seg(segment(buf, buf_len, offset));
    offset = seg.first;
    const size_t seg_end(seg.first + seg.second);
    assert(seg_end <= buf_len);

    while (offset < seg_end)
    {
        Key key(version);
        if ((offset = unserialize(buf, buf_len, offset, key)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        ks.push_back(key);
    }
    assert(offset == seg_end);
    return offset;
}

void galera::WriteSet::append_key(const Key& key)
{
    const size_t hash(key.hash());

    std::pair<KeyRefMap::const_iterator, KeyRefMap::const_iterator>
        range(key_refs_.equal_range(hash));

    for (KeyRefMap::const_iterator i(range.first); i != range.second; ++i)
    {
        Key cmp(version_);

        (void)unserialize(&keys_[0], keys_.size(), i->second, cmp);

        if (key == cmp && key.flags() == cmp.flags()) return;
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
        Key key(version_);
        if ((offset = unserialize(&keys_[0], keys_.size(), offset, key)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        s.push_back(key);
    }
    assert(offset == keys_.size());
}
