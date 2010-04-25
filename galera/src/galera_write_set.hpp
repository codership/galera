
#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "write_set.hpp"

#include <vector>
#include <limits>

namespace galera
{
    class RowKey
    {
    public:
        RowKey() : dbtable_(), key_(), action_() { }
        RowKey(const void* dbtable, size_t dbtable_len,
               const void* key, size_t key_len, int action)
            :
            dbtable_(reinterpret_cast<const gu::byte_t*>(dbtable),
                     reinterpret_cast<const gu::byte_t*>(dbtable) 
                     + dbtable_len),
            key_(reinterpret_cast<const gu::byte_t*>(key),
                 reinterpret_cast<const gu::byte_t*>(key) + key_len),
            action_(action) { }
        const gu::Buffer& get_dbtable() const { return dbtable_; }
        const gu::Buffer& get_key() const { return key_; }
        
        void clear() { dbtable_.clear(); key_.clear(); }
    private:
        friend size_t serialize(const RowKey&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, RowKey&);
        friend size_t serial_size(const RowKey&);
        gu::Buffer dbtable_;
        gu::Buffer key_;
        int action_;
    };
    

    size_t serialize(const RowKey&, gu::byte_t*, size_t, size_t);
    size_t unserialize(const gu::byte_t*, size_t, size_t, RowKey&);
    size_t serial_size(const RowKey&);
    

    typedef gu::Buffer Query;

    class GaleraWriteSet : public WriteSet
    {
    public:
        typedef std::vector<Query> QuerySequence;
        typedef std::vector<RowKey> RowKeySequence;        

        GaleraWriteSet() 
            : 
            type_(),
            level_(),
            last_seen_trx_(),
            queries_(),
            keys_(),
            rbr_()
        { }
        

        enum wsdb_ws_type get_type() const { return type_; }
        enum wsdb_ws_level get_level() const { return level_; }
        wsrep_seqno_t get_last_seen_trx() const { return last_seen_trx_; }
        const gu::Buffer& get_rbr() const { return rbr_; }
        
        void append_query(const void* query, size_t query_len)
        {
            queries_.push_back(Query(reinterpret_cast<const gu::byte_t*>(query),
                                     reinterpret_cast<const gu::byte_t*>(query)
                                     + query_len));
        }
        
        void append_row_key(const void* dbtable, size_t dbtable_len,
                            const void* key, size_t key_len,
                            int action)
        {
            keys_.push_back(RowKey(dbtable, dbtable_len, 
                                   key, key_len, action));
        }
        
        void assign_rbr(const void* rbr_data, size_t rbr_data_len)
        {
            assert(rbr_.empty() == true);
            rbr_.insert(rbr_.begin(),
                        reinterpret_cast<const gu::byte_t*>(rbr_data),
                        reinterpret_cast<const gu::byte_t*>(rbr_data) + rbr_data_len);
        }
        
        const RowKeySequence& get_keys() const { return keys_; }
        const QuerySequence& get_queries() const { return queries_; }
        
    private:
        friend size_t serialize(const GaleraWriteSet&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, GaleraWriteSet&);
        friend size_t serial_size(const GaleraWriteSet&);
        
        enum wsdb_ws_type type_;
        enum wsdb_ws_level level_;
        wsrep_seqno_t last_seen_trx_;
        QuerySequence queries_;
        RowKeySequence keys_;
        gu::Buffer rbr_;
    };
    
    size_t serialize(const GaleraWriteSet& ws, gu::byte_t* buf, 
                     size_t buf_len, size_t offset);
    
    size_t unserialize(const gu::byte_t* buf, size_t buf_len,
                       size_t offset, GaleraWriteSet& ws);
    
    size_t serial_size(const GaleraWriteSet& ws);
}


#endif // GALERA_WRITE_SET_HPP
