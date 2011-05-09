//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "wsrep_api.h"
#include "gu_buffer.hpp"
#include "gu_logger.hpp"
#include "gu_unordered.hpp"

#include <vector>
#include <deque>

#include <cstring>


namespace galera
{
    class RowId
    {
    public:

        RowId(const void* table     = 0,
              uint16_t    table_len = 0,
              const void* key       = 0,
              uint16_t    key_len   = 0)
            :
            table_    (table),
            key_      (key),
            table_len_(table_len),
            key_len_  (key_len)
        { }

        const void* get_table()     const { return table_;     }
        size_t      get_table_len() const { return table_len_; }
        const void* get_key()       const { return key_;       }
        size_t      get_key_len()   const { return key_len_;   }

        size_t get_hash() const
        {
            size_t prime(5381);
            const gu::byte_t* tb(reinterpret_cast<const gu::byte_t*>(table_));
            const gu::byte_t* const te(tb + table_len_);

            for (; tb != te; ++tb)
            {
                prime = ((prime << 5) + prime) + *tb;
            }

            const gu::byte_t* kb(reinterpret_cast<const gu::byte_t*>(key_));
            const gu::byte_t* const ke(kb + key_len_);

            for (; kb != ke; ++kb)
            {
                prime = ((prime << 5) + prime) + *kb;
            }

            return prime;
        }


    private:

        friend size_t serialize(const RowId&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, RowId&);
        friend size_t serial_size(const RowId&);
        friend bool operator==(const RowId& a, const RowId& b);
        const void* table_;
        const void* key_;
        uint16_t    table_len_;
        uint16_t    key_len_;
    };

    inline bool operator==(const RowId& a, const RowId& b)
    {
        return (a.key_len_   == b.key_len_                &&
                a.table_len_ == b.table_len_              &&
                !memcmp(a.key_, b.key_, a.key_len_)       &&
                !memcmp(a.table_, b.table_, a.table_len_));
    }

    typedef std::deque<RowId> RowIdSequence;

    // This is needed for unordered map
    class RowIdHash
    {
    public:
        size_t operator() (const RowId& ri) const { return ri.get_hash(); }
    };

    class WriteSet
    {
    public:
        WriteSet()
            :
            row_ids_(),
            row_id_refs_(),
            data_()
        { }

        const gu::Buffer& get_data() const { return data_; }

        void append_row_id(const void* dbtable, size_t dbtable_len,
                           const void* key, size_t key_len);


        void append_data(const void*data, size_t data_len)
        {
            data_.reserve(data_.size() + data_len);
            data_.insert(data_.end(),
                         reinterpret_cast<const gu::byte_t*>(data),
                         reinterpret_cast<const gu::byte_t*>(data) + data_len);
        }

        void get_row_ids(RowIdSequence&) const;
        const gu::Buffer& get_key_buf() const { return row_ids_; }
        bool empty() const
        {
            return (data_.size() == 0 && row_ids_.size() == 0);
        }

        void clear() { row_ids_.clear(), row_id_refs_.clear(), data_.clear(); }

    private:

        friend size_t serialize(const WriteSet&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, WriteSet&);
        friend size_t serial_size(const WriteSet&);

        typedef gu::UnorderedMultimap<size_t, size_t> RowIdRefMap;

        gu::Buffer         row_ids_;
        RowIdRefMap        row_id_refs_;
        gu::Buffer         data_;
    };
}


#endif // GALERA_WRITE_SET_HPP
