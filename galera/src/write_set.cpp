//
// Copyright (C) 2010-2013 Codership Oy <info@codership.com>
//


#include "write_set.hpp"

#include "gu_serialize.hpp"
#include "gu_logger.hpp"


size_t galera::WriteSet::serialize(gu::byte_t* buf,
                                   size_t      buf_len,
                                   size_t      offset) const
{
    offset = gu::serialize4(keys_, buf, buf_len, offset);
    offset = gu::serialize4(data_, buf, buf_len, offset);
    return offset;
}


size_t galera::WriteSet::unserialize(const gu::byte_t* buf,
                                     size_t            buf_len,
                                     size_t            offset)
{
    keys_.clear();
    offset = gu::unserialize4(buf, buf_len, offset, keys_);
    offset = gu::unserialize4(buf, buf_len, offset, data_);
    return offset;
}


size_t galera::WriteSet::serial_size() const
{
    return (gu::serial_size4(keys_) + gu::serial_size4(data_));
}

std::pair<size_t, size_t>
galera::WriteSet::segment(const gu::byte_t* buf, size_t buf_len, size_t offset)
{
    uint32_t data_len;
    offset = gu::unserialize4(buf, buf_len, offset, data_len);
    if (gu_unlikely(offset + data_len > buf_len))
    {
#ifdef NDEBUG
        gu_throw_error(EMSGSIZE);
#else
        gu_throw_error(EMSGSIZE) << "offset: " << offset << ", data_len: "
                                 << data_len << ", buf_len: " << buf_len;
#endif /* NDEBUG */
    }
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
        KeyOS key(version);
        if ((offset = key.unserialize(buf, buf_len, offset)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        ks.push_back(key);
    }
    assert(offset == seg_end);
    return offset;
}

void galera::WriteSet::append_key(const KeyData& kd)
{
    KeyOS key (kd.proto_ver,
               kd.parts,
               kd.parts_num,
               (kd.shared() ? galera::KeyOS::F_SHARED : 0)
               );

    const size_t hash(key.hash());

    std::pair<KeyRefMap::const_iterator, KeyRefMap::const_iterator>
        range(key_refs_.equal_range(hash));

    for (KeyRefMap::const_iterator i(range.first); i != range.second; ++i)
    {
        KeyOS cmp(version_);

        (void)cmp.unserialize(&keys_[0], keys_.size(), i->second);

        if (key == cmp && key.flags() == cmp.flags()) return;
    }

    size_t key_size(key.serial_size());
    size_t offset(keys_.size());
    keys_.resize(offset + key_size);
    (void)key.serialize(&keys_[0], keys_.size(), offset);
    (void)key_refs_.insert(std::make_pair(hash, offset));
}


void galera::WriteSet::get_keys(KeySequence& s) const
{
    size_t offset(0);
    while (offset < keys_.size())
    {
        KeyOS key(version_);
        if ((offset = key.unserialize(&keys_[0], keys_.size(), offset)) == 0)
        {
            gu_throw_fatal << "failed to unserialize key";
        }
        s.push_back(key);
    }
    assert(offset == keys_.size());
}
