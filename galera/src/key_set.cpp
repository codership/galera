//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//


#include "key_set.hpp"

#include "gu_serialize.hpp"
#include "gu_logger.hpp"


std::pair<size_t, size_t>
galera::KeySetIn::segment(const gu::byte_t* buf, size_t buf_len, size_t offset)
{
    uint32_t data_len;
    offset = gu::unserialize4(buf, buf_len, offset, data_len);
    if (offset + data_len > buf_len) gu_throw_error(EMSGSIZE);
    return std::pair<size_t, size_t>(offset, data_len);
}

size_t galera::KeySetIn::keys(const gu::byte_t* buf,
                              size_t buf_len, size_t offset, int version,
                              KeySequence& ks)
{
    std::pair<size_t, size_t> seg(segment(buf, buf_len, offset));
    offset = seg.first;
    const size_t seg_end(seg.first + seg.second);
    assert(seg_end <= buf_len);

    while (offset < seg_end)
    {
        KeyOS key(version);
        if ((offset = unserialize(buf, buf_len, offset, key)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        ks.push_back(key);
    }
    assert(offset == seg_end);
    return offset;
}

void galera::KeySetOut::append_key(const KeyOS& key)
{
    const size_t hash(key.hash());

    std::pair<KeyRefMap::const_iterator, KeyRefMap::const_iterator>
        range(key_refs_.equal_range(hash));

    for (KeyRefMap::const_iterator i(range.first); i != range.second; ++i)
    {
        KeyOS cmp(version_);

        (void)unserialize(&keys_[0], keys_.size(), i->second, cmp);

        if (key == cmp && key.flags() == cmp.flags()) return;
    }

    size_t key_size(serial_size(key));
    size_t offset(keys_.size());
    keys_.resize(offset + key_size);
    (void)galera::serialize(key, &keys_[0], keys_.size(), offset);
    (void)key_refs_.insert(std::make_pair(hash, offset));
}


void galera::KeySetIn::get_keys(KeySequence& s) const
{
    size_t offset(0);
    while (offset < keys_.size())
    {
        KeyOS key(version_);
        if ((offset = unserialize(&keys_[0], keys_.size(), offset, key)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        s.push_back(key);
    }
    assert(offset == keys_.size());
}
