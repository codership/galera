//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "key.hpp"

#include "wsrep_api.h"
#include "gu_buffer.hpp"
#include "gu_logger.hpp"
#include "gu_unordered.hpp"

#include <vector>
#include <deque>

#include <cstring>

namespace galera
{
    class WriteSet
    {
    public:
        typedef std::deque<Key> KeySequence;

        WriteSet()
            :
            keys_(),
            key_refs_(),
            data_()
        { }

        const gu::Buffer& get_data() const { return data_; }

        void append_key(const Key&);

        void append_data(const void*data, size_t data_len)
        {
            data_.reserve(data_.size() + data_len);
            data_.insert(data_.end(),
                         reinterpret_cast<const gu::byte_t*>(data),
                         reinterpret_cast<const gu::byte_t*>(data) + data_len);
        }

        void get_keys(KeySequence&) const;
        const gu::Buffer& get_key_buf() const { return keys_; }
        bool empty() const
        {
            return (data_.size() == 0 && keys_.size() == 0);
        }

        void clear() { keys_.clear(), key_refs_.clear(), data_.clear(); }

    private:

        friend size_t serialize(const WriteSet&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, WriteSet&);
        friend size_t serial_size(const WriteSet&);

        typedef gu::UnorderedMultimap<size_t, size_t> KeyRefMap;

        gu::Buffer         keys_;
        KeyRefMap          key_refs_;
        gu::Buffer         data_;
    };
}


#endif // GALERA_WRITE_SET_HPP
