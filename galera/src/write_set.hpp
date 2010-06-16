//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "wsdb_api.h"
#include "wsrep_api.h"
#include "gu_buffer.hpp"
#include "gu_logger.hpp"
#include "gu_unordered.hpp"

#include <vector>
#include <deque>

#include <cstring>


namespace galera
{
    class Query
    {
    public:
        Query(const void* query = 0,
              size_t query_len = 0,
              time_t tstamp = -1,
              uint32_t rnd_seed = 0) :
            query_(reinterpret_cast<const gu::byte_t*>(query),
                   reinterpret_cast<const gu::byte_t*>(query) + query_len),
            tstamp_(tstamp),
            rnd_seed_(rnd_seed)
        { }

        const gu::Buffer& get_query() const { return query_; }
        time_t get_tstamp() const { return tstamp_; }
        uint32_t get_rnd_seed() const { return rnd_seed_; }
    private:
        friend size_t serialize(const Query&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, Query&);
        friend size_t serial_size(const Query&);
        gu::Buffer query_;
        time_t tstamp_;
        uint32_t rnd_seed_;
    };

    std::ostream& operator<<(std::ostream&, const Query& q);

    typedef std::deque<Query> QuerySequence;

    class RowKey
    {
    public:

        RowKey(const void* table     = 0,
               uint16_t    table_len = 0,
               const void* key       = 0,
               uint16_t    key_len   = 0,
               gu::byte_t  action    = 0)
            :
            table_    (table),
            key_      (key),
            table_len_(table_len),
            key_len_  (key_len),
            action_   (action)
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

        friend size_t serialize(const RowKey&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, RowKey&);
        friend size_t serial_size(const RowKey&);
        friend bool operator==(const RowKey& a, const RowKey& b);
        const void* table_;
        const void* key_;
        uint16_t    table_len_;
        uint16_t    key_len_;
        gu::byte_t  action_;
    };

    inline bool operator==(const RowKey& a, const RowKey&b)
    {
        return (a.table_len_ == b.table_len_              &&
                a.key_len_   == b.key_len_                &&
                !memcmp(a.table_, b.table_, a.table_len_) &&
                !memcmp(a.key_, b.key_, a.key_len_));
    }

    typedef std::deque<RowKey> RowKeySequence;

    // This is needed for unordered map
    class RowKeyHash
    {
    public:
        size_t operator() (const RowKey& rk) const { return rk.get_hash(); }
    };

    class WriteSet
    {
    public:
        WriteSet()
            :
            level_(WSDB_WS_QUERY),
            queries_(),
            keys_(),
            key_refs_(),
            data_()
        { }

        enum wsdb_ws_level  get_level()     const { return level_;     }

        const gu::Buffer& get_data() const { return data_; }

        void append_query(const void* query, size_t query_len,
                          time_t tstamp = -1,
                          uint32_t rndseed = -1)
        {
            queries_.push_back(Query(query,
                                     query_len, tstamp, rndseed));
        }

        void prepend_query(const Query& query)
        {
            queries_.push_front(query);
        }

        void append_row_key(const void* dbtable, size_t dbtable_len,
                            const void* key, size_t key_len,
                            int action);


        void append_data(const void*data, size_t data_len)
        {
            data_.reserve(data_.size() + data_len);
            data_.insert(data_.end(),
                         reinterpret_cast<const gu::byte_t*>(data),
                         reinterpret_cast<const gu::byte_t*>(data) + data_len);
            level_ = WSDB_WS_DATA_RBR;
        }

        void get_keys(RowKeySequence&) const;
        const gu::Buffer& get_key_buf() const { return keys_; }
        const QuerySequence& get_queries() const { return queries_; }
        bool is_empty() const
        {
            return (data_.size() == 0 && keys_.size() == 0 && queries_.size() == 0);
        }

        void clear() { keys_.clear(), key_refs_.clear(),
                data_.clear(), queries_.clear(); }

    private:

        friend size_t serialize(const WriteSet&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, WriteSet&);
        friend size_t serial_size(const WriteSet&);

        typedef gu::UnorderedMultimap<size_t, size_t> KeyRefMap;

        enum wsdb_ws_level level_;
        QuerySequence      queries_;
        gu::Buffer         keys_;
        KeyRefMap          key_refs_;
        gu::Buffer         data_;
    };

    inline bool operator==(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
    {
        return (memcmp(&a, &b, sizeof(a)) == 0);
    }

    inline bool operator!=(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
    {
        return !(a == b);
    }
}


#endif // GALERA_WRITE_SET_HPP
