//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "wsdb_api.h"
#include "wsrep_api.h"
#include "gu_buffer.hpp"
#include "gu_logger.hpp"

#include <vector>
#include <deque>

#include <boost/unordered_map.hpp>

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
        RowKey(const void* dbtable = 0, uint16_t dbtable_len = 0,
               const void* key = 0, uint16_t key_len = 0, gu::byte_t action = 0)
            :
            dbtable_(dbtable),
            dbtable_len_(dbtable_len),
            key_(key),
            key_len_(key_len),
            action_(action) 
        { }
        const void* get_dbtable() const { return dbtable_; }
        size_t get_dbtable_len() const { return dbtable_len_; }
        const void* get_key() const { return key_; }
        size_t get_key_len() const { return key_len_; }
    private:
        friend size_t serialize(const RowKey&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, RowKey&);
        friend size_t serial_size(const RowKey&);
        friend bool operator==(const RowKey& a, const RowKey& b);
        const void* dbtable_;
        uint16_t dbtable_len_;
        const void* key_;
        uint16_t key_len_;
        gu::byte_t action_;
    };
    
    inline bool operator==(const RowKey& a, const RowKey&b)
    {
        if (a.dbtable_len_ != b.dbtable_len_) return false;
        if (a.key_len_ != b.key_len_) return false;
        if (memcmp(a.dbtable_, b.dbtable_, a.dbtable_len_) != 0) return false;
        return (memcmp(a.key_, b.key_, a.key_len_) == 0);
    }
    
    typedef std::deque<RowKey> RowKeySequence;
    
    class RowKeyHash
    {
    public:
        size_t operator()(const RowKey& rk) const
        {
            const gu::byte_t* b(reinterpret_cast<const gu::byte_t*>(
                                    rk.get_key()));
            const gu::byte_t* e(reinterpret_cast<const gu::byte_t*>(
                                    rk.get_key()) + rk.get_key_len());
            return boost::hash_range(b, e);
        }
    };
    
    
    class WriteSet 
    {
    public:
        enum
        {
            F_COMMIT = 1 << 0,
            F_ROLLBACK = 1 << 1
        };
        
        WriteSet() 
            : 
            source_id_(WSREP_UUID_UNDEFINED),
            conn_id_(-1),
            trx_id_(-1),
            type_(),
            level_(WSDB_WS_QUERY),
            flags_(0),
            last_seen_trx_(),
            queries_(),
            keys_(),
            key_refs_(),
            data_()
        { }
        
        WriteSet(const wsrep_uuid_t& source_id, 
                 wsrep_conn_id_t conn_id,
                 wsrep_trx_id_t trx_id,
                 enum wsdb_ws_type type) 
            : 
            source_id_(source_id),
            conn_id_(conn_id),
            trx_id_(trx_id),
            type_(type),
            level_(WSDB_WS_QUERY),
            flags_(0),
            last_seen_trx_(),
            queries_(),
            keys_(),
            key_refs_(),
            data_()
        { }
        
        const wsrep_uuid_t& get_source_id() const { return source_id_; }
        wsrep_conn_id_t get_conn_id() const { return conn_id_; }
        wsrep_trx_id_t get_trx_id() const { return trx_id_; }
        enum wsdb_ws_type get_type() const { return type_; }
        enum wsdb_ws_level get_level() const { return level_; }
        void assign_flags(int flags) { flags_ = flags; }
        int get_flags() const { return flags_; }
        
        void assign_last_seen_trx(wsrep_seqno_t seqno) { last_seen_trx_ = seqno; }
        wsrep_seqno_t get_last_seen_trx() const { return last_seen_trx_; }
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
        bool empty() const { return (data_.size() == 0 && queries_.size() == 0); }
        void serialize(gu::Buffer& buf) const;
        void clear() { keys_.clear(), key_refs_.clear(),
                data_.clear(), queries_.clear(); }
    private:
        friend size_t serialize(const WriteSet&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, WriteSet&, bool skip_data = false);
        friend size_t serial_size(const WriteSet&);
        
        wsrep_uuid_t   source_id_;
        wsrep_conn_id_t conn_id_;
        wsrep_trx_id_t trx_id_;
        
        enum wsdb_ws_type type_;
        enum wsdb_ws_level level_;
        int flags_;
        wsrep_seqno_t last_seen_trx_;
        QuerySequence queries_;
        gu::Buffer keys_;
        typedef boost::unordered_multimap<size_t, size_t> KeyRefMap;
        KeyRefMap key_refs_;
        gu::Buffer data_;
    };
    
    inline bool operator==(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
    {
        return (memcmp(&a, &b, sizeof(a)) == 0);
    }
}


#endif // GALERA_WRITE_SET_HPP
