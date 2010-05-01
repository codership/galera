//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WRITE_SET_HPP
#define GALERA_WRITE_SET_HPP

#include "wsdb_api.h"
#include "wsrep_api.h"
#include "gu_buffer.hpp"

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
        gu::Buffer query_;
        time_t tstamp_;
        uint32_t rnd_seed_;
    };

    inline std::ostream& operator<<(std::ostream& os, const Query& q)
    {
        return (os << q);
    }

    typedef std::vector<Query> QuerySequence;
    
    class WriteSet
    {
    public:
        WriteSet() { }
        virtual ~WriteSet() { }
        virtual enum wsdb_ws_type get_type() const = 0;
        virtual enum wsdb_ws_level get_level() const = 0;
        virtual wsrep_seqno_t get_last_seen_trx() const = 0;
        
        virtual void append_query(const void* query, size_t query_len) { }
        virtual void assign_rbr(const void* rbr_data, size_t rbr_data_len) { }
        virtual void append_row_key(const void* dbtable, 
                                    size_t dbtable_len,
                                    const void* key, 
                                    size_t key_len,
                                    int action) { }
        virtual const QuerySequence& get_queries() const = 0;
        virtual const gu::Buffer& get_rbr() const = 0;
        
        virtual bool empty() const = 0;
        virtual void serialize(gu::Buffer&) const = 0;
        
    private:
    };


}


#endif // GALERA_WRITE_SET_HPP
