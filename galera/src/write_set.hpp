//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "key_os.hpp"
#include "key_data.hpp"

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
        typedef std::deque<KeyOS> KeySequence;

        WriteSet(int version)
            :
            version_(version),
            keys_(),
            key_refs_(),
            data_()
        { }

        void set_version(int version) { version_ = version; }
        const gu::Buffer& get_data() const { return data_; }

        void append_key(const KeyData&);

        void append_data(const void*data, size_t data_len)
        {
            data_.reserve(data_.size() + data_len);
            data_.insert(data_.end(),
                         static_cast<const gu::byte_t*>(data),
                         static_cast<const gu::byte_t*>(data) + data_len);
        }

        void get_keys(KeySequence&) const;
        const gu::Buffer& get_key_buf() const { return keys_; }
        bool empty() const
        {
            return (data_.size() == 0 && keys_.size() == 0);
        }

        void clear() { keys_.clear(), key_refs_.clear(), data_.clear(); }

        // Return offset to beginning of key or data segment and length
        // of that segment
        static std::pair<size_t, size_t>
        segment(const gu::byte_t*, size_t, size_t);

        // Scan key sequence from buffer, return offset from the beginning of
        // buffer after scan.
        static size_t keys(const gu::byte_t*, size_t, size_t, int, KeySequence&);

        size_t serialize(gu::byte_t*, size_t, size_t) const;
        size_t unserialize(const gu::byte_t*, size_t, size_t);
        size_t serial_size() const;

    private:
        typedef gu::UnorderedMultimap<size_t, size_t> KeyRefMap;

        int                version_;
        gu::Buffer         keys_;
        KeyRefMap          key_refs_;
        gu::Buffer         data_;
    };
}


#endif // GALERA_WRITE_SET_HPP
